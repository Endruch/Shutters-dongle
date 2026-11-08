from __future__ import annotations

import asyncio
from dataclasses import dataclass
import hashlib
import hmac
import json
import secrets
from typing import Any

import aiohttp

KDF_ROUNDS = 50000
TIMEOUT = aiohttp.ClientTimeout(total=10)
UPLOAD_TIMEOUT = aiohttp.ClientTimeout(total=180)


class DongleError(Exception):
    pass


class DongleConnectionError(DongleError):
    pass


class DongleRateLimited(DongleConnectionError):
    pass


class DongleAuthError(DongleError):
    pass


class DongleTooOld(DongleError):
    pass


@dataclass(frozen=True)
class DongleInfo:
    mac: str
    name: str
    fw: str
    model: str
    pw_set: bool


@dataclass(frozen=True)
class Channel:
    ch: int
    name: str
    proto: int
    state: int


@dataclass(frozen=True)
class DongleState:
    radio_ok: bool
    channels: dict[int, Channel]


class DongleClient:
    def __init__(self, session: aiohttp.ClientSession, host: str, password: str) -> None:
        self._session = session
        self._base = f"http://{host}"
        self._password = password
        self._token: str | None = None
        self._key: bytes | None = None
        self._auth_lock = asyncio.Lock()

    async def _send(
        self,
        method: str,
        path: str,
        data: dict[str, Any] | bytes | None = None,
        headers: dict[str, str] | None = None,
        timeout: aiohttp.ClientTimeout = TIMEOUT,
    ) -> tuple[int, str]:
        try:
            async with self._session.request(
                method,
                self._base + path,
                data=data,
                headers=headers,
                timeout=timeout,
                allow_redirects=False,
            ) as resp:
                return resp.status, await resp.text()
        except (aiohttp.ClientError, asyncio.TimeoutError) as err:
            raise DongleConnectionError(str(err) or type(err).__name__) from err

    @staticmethod
    def _check(status: int, body: str) -> str:
        if status == 401 or 300 <= status < 400:
            raise DongleAuthError(body or f"HTTP {status}")
        if status == 429:
            raise DongleRateLimited(body)
        if status != 200:
            raise DongleConnectionError(f"HTTP {status}: {body}")
        return body

    @staticmethod
    def _field(body: str, key: str) -> str:
        try:
            return str(json.loads(body)[key])
        except (ValueError, KeyError, TypeError) as err:
            raise DongleConnectionError("unexpected auth reply") from err

    async def _auth_key(self) -> bytes:
        if self._key is None:
            mac = (await self.get_info()).mac
            raw = await asyncio.get_running_loop().run_in_executor(
                None, hashlib.pbkdf2_hmac, "sha256", self._password.encode(), f"shuttersdongle:{mac}".encode(), KDF_ROUNDS
            )
            self._key = raw.hex().encode()
        return self._key

    async def _handshake(self, scope: str | None = None) -> str:
        status, body = await self._send("GET", "/api/auth/challenge")
        if status == 404:
            raise DongleTooOld("the dongle needs newer firmware")
        nonce = self._field(self._check(status, body), "n")
        key = await self._auth_key()
        cnonce = secrets.token_hex(16)
        proof = hmac.new(key, f"shuttersdongle-auth:{nonce}:{cnonce}".encode(), hashlib.sha256).hexdigest()
        data = {"n": nonce, "c": cnonce, "h": proof}
        if scope:
            data["scope"] = scope
        status, body = await self._send("POST", "/api/auth/token", data=data)
        body = self._check(status, body)
        token = self._field(body, "t")
        expected = hmac.new(key, f"shuttersdongle-dongle:{nonce}:{cnonce}:{token}".encode(), hashlib.sha256).hexdigest()
        if not hmac.compare_digest(self._field(body, "p"), expected):
            raise DongleConnectionError("the device at this address could not prove it is the dongle")
        return token

    async def _login(self, stale: str | None) -> str:
        async with self._auth_lock:
            if self._token and self._token != stale:
                return self._token
            self._token = await self._handshake()
            return self._token

    async def _request(
        self,
        method: str,
        path: str,
        data: dict[str, Any] | bytes | None = None,
        auth: bool = True,
        headers: dict[str, str] | None = None,
        timeout: aiohttp.ClientTimeout = TIMEOUT,
    ) -> str:
        if not auth:
            return self._check(*await self._send(method, path, data, headers, timeout))
        token = self._token or await self._login(None)
        for retry in (False, True):
            status, body = await self._send(
                method, path, data, {**(headers or {}), "Authorization": f"Bearer {token}"}, timeout
            )
            if status != 401 or retry:
                return self._check(status, body)
            token = await self._login(token)
        raise DongleAuthError("unreachable")

    async def _json(self, path: str, auth: bool = True) -> Any:
        body = await self._request("GET", path, auth=auth)
        try:
            return json.loads(body)
        except ValueError as err:
            raise DongleConnectionError(f"bad JSON from {path}") from err

    async def get_info(self) -> DongleInfo:
        j = await self._json("/api/info", auth=False)
        try:
            return DongleInfo(
                mac=str(j["mac"]).lower(),
                name=str(j["name"]),
                fw=str(j["fw"]),
                model=str(j["model"]),
                pw_set=bool(j["pw_set"]),
            )
        except (KeyError, TypeError) as err:
            raise DongleConnectionError("not a Shutters Dongle") from err

    async def get_state(self) -> DongleState:
        j = await self._json("/api/state")
        try:
            channels = {
                int(c["ch"]): Channel(int(c["ch"]), str(c["name"]), int(c["proto"]), int(c["state"]))
                for c in j["ch"]
            }
            return DongleState(radio_ok=bool(j["radio"]), channels=channels)
        except (KeyError, TypeError, ValueError) as err:
            raise DongleConnectionError("unexpected /api/state reply") from err

    async def link(self) -> None:
        await self._request("POST", "/api/ha/link")

    async def send_command(self, ch: int, cmd: str) -> None:
        await self._request("POST", "/api/shutter/cmd", data={"ch": ch, "cmd": cmd})

    async def upload_firmware(self, image: bytes, filename: str) -> None:
        boundary = "----ShuttersDongleFirmware"
        body = (
            f'--{boundary}\r\nContent-Disposition: form-data; name="fw"; filename="{filename}"\r\n'
            "Content-Type: application/octet-stream\r\n\r\n"
        ).encode() + image + f"\r\n--{boundary}--\r\n".encode()
        ticket = await self._handshake("update")
        reply = self._check(
            *await self._send(
                "POST",
                "/update",
                body,
                {"Content-Type": f"multipart/form-data; boundary={boundary}", "Authorization": f"Bearer {ticket}"},
                UPLOAD_TIMEOUT,
            )
        )
        if not reply.startswith("OK"):
            raise DongleError(f"the dongle rejected the firmware: {reply}")
