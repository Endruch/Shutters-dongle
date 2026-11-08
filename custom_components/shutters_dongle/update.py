from __future__ import annotations

import asyncio
from dataclasses import dataclass
from datetime import timedelta
import logging
import re
import time
from typing import Any

import aiohttp

from homeassistant.components.update import UpdateDeviceClass, UpdateEntity, UpdateEntityFeature
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from . import DongleConfigEntry
from .api import DongleError
from .const import DOMAIN, GITHUB_REPO
from .coordinator import DongleCoordinator

_LOGGER = logging.getLogger(__name__)

SCAN_INTERVAL = timedelta(hours=6)
REBOOT_WAIT = 90


@dataclass
class Release:
    version: str
    url: str
    notes: str
    asset_name: str
    asset_url: str


def _version_key(v: str) -> tuple[int, ...]:
    return tuple(int(n) for n in re.findall(r"\d+", v.split("-")[0]))


async def async_setup_entry(hass: HomeAssistant, entry: DongleConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback) -> None:
    async_add_entities([DongleFirmwareUpdate(entry.runtime_data)], update_before_add=True)


class DongleFirmwareUpdate(CoordinatorEntity[DongleCoordinator], UpdateEntity):
    _attr_has_entity_name = True
    _attr_device_class = UpdateDeviceClass.FIRMWARE
    _attr_entity_category = EntityCategory.CONFIG
    _attr_supported_features = UpdateEntityFeature.INSTALL | UpdateEntityFeature.PROGRESS | UpdateEntityFeature.RELEASE_NOTES
    _attr_title = "Shutters Dongle firmware"

    def __init__(self, coordinator: DongleCoordinator) -> None:
        super().__init__(coordinator)
        mac = coordinator.info.mac
        self._attr_unique_id = f"{mac}_firmware"
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, mac)})
        self._release: Release | None = None

    @property
    def should_poll(self) -> bool:
        return True

    @property
    def installed_version(self) -> str:
        return self.coordinator.info.fw

    @property
    def latest_version(self) -> str | None:
        return self._release.version if self._release else self.installed_version

    @property
    def release_url(self) -> str | None:
        return self._release.url if self._release else None

    def version_is_newer(self, latest_version: str, installed_version: str) -> bool:
        return _version_key(latest_version) > _version_key(installed_version)

    async def async_release_notes(self) -> str | None:
        return self._release.notes if self._release else None

    async def async_update(self) -> None:
        session = async_get_clientsession(self.hass)
        try:
            async with session.get(
                f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest",
                headers={"Accept": "application/vnd.github+json"},
                timeout=aiohttp.ClientTimeout(total=20),
            ) as resp:
                if resp.status != 200:
                    _LOGGER.debug("GitHub release check: HTTP %s", resp.status)
                    return
                j = await resp.json()
        except (aiohttp.ClientError, asyncio.TimeoutError, ValueError) as err:
            _LOGGER.debug("GitHub release check failed: %s", err)
            return
        asset = next(
            (a for a in j.get("assets", []) if a.get("name", "").endswith(".bin") and not a["name"].endswith("-full.bin")),
            None,
        )
        if not asset or not j.get("tag_name"):
            return
        self._release = Release(
            version=j["tag_name"].removeprefix("v"),
            url=j.get("html_url", ""),
            notes=j.get("body") or "",
            asset_name=asset["name"],
            asset_url=asset["browser_download_url"],
        )

    async def async_install(self, version: str | None, backup: bool, **kwargs: Any) -> None:
        release = self._release
        if release is None:
            raise HomeAssistantError("No firmware release found on GitHub")
        client = self.coordinator.client
        self._attr_in_progress = True
        self.async_write_ha_state()
        try:
            try:
                async with async_get_clientsession(self.hass).get(
                    release.asset_url, timeout=aiohttp.ClientTimeout(total=120)
                ) as resp:
                    resp.raise_for_status()
                    image = await resp.read()
            except (aiohttp.ClientError, asyncio.TimeoutError) as err:
                raise HomeAssistantError(f"Couldn't download {release.asset_name}: {err}") from err

            try:
                await client.upload_firmware(image, release.asset_name)
            except DongleError as err:
                raise HomeAssistantError(f"Firmware upload failed: {err}") from err

            deadline = time.monotonic() + REBOOT_WAIT
            await asyncio.sleep(5)
            while True:
                try:
                    info = await client.get_info()
                    if info.fw != self.coordinator.info.fw:
                        break
                except DongleError:
                    pass
                if time.monotonic() > deadline:
                    raise HomeAssistantError("The dongle didn't come back with the new firmware; check it in its web interface")
                await asyncio.sleep(3)

            self.coordinator.info = info
            reg = dr.async_get(self.hass)
            if device := reg.async_get_device(identifiers={(DOMAIN, info.mac)}):
                reg.async_update_device(device.id, sw_version=info.fw)
            await self.coordinator.async_request_refresh()
        finally:
            self._attr_in_progress = False
            self.async_write_ha_state()
