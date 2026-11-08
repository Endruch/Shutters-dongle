from __future__ import annotations

from collections.abc import Mapping
from typing import Any

import voluptuous as vol

from homeassistant.config_entries import ConfigFlow, ConfigFlowResult
from homeassistant.const import CONF_HOST, CONF_PASSWORD
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo

from .api import DongleAuthError, DongleClient, DongleConnectionError, DongleInfo, DongleRateLimited, DongleTooOld
from .const import DOMAIN

PASSWORD_SCHEMA = vol.Schema({vol.Required(CONF_PASSWORD): str})
USER_SCHEMA = vol.Schema({vol.Required(CONF_HOST): str, vol.Required(CONF_PASSWORD): str})


class DongleConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._host: str | None = None
        self._info: DongleInfo | None = None

    async def _probe(self, host: str, password: str) -> tuple[DongleInfo | None, str | None]:
        client = DongleClient(async_get_clientsession(self.hass), host, password)
        try:
            info = await client.get_info()
            if not info.pw_set:
                return info, "password_not_set"
            await client.get_state()
        except DongleAuthError:
            return None, "invalid_auth"
        except DongleTooOld:
            return None, "firmware_too_old"
        except DongleRateLimited:
            return None, "rate_limited"
        except DongleConnectionError:
            return None, "cannot_connect"
        return info, None

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            host = user_input[CONF_HOST].strip()
            info, error = await self._probe(host, user_input[CONF_PASSWORD])
            if error:
                errors["base"] = error
            else:
                await self.async_set_unique_id(info.mac)
                self._abort_if_unique_id_configured(updates={CONF_HOST: host})
                return self.async_create_entry(title=info.name, data={CONF_HOST: host, CONF_PASSWORD: user_input[CONF_PASSWORD]})
        return self.async_show_form(
            step_id="user",
            data_schema=self.add_suggested_values_to_schema(USER_SCHEMA, user_input),
            errors=errors,
        )

    async def async_step_zeroconf(self, discovery_info: ZeroconfServiceInfo) -> ConfigFlowResult:
        if discovery_info.ip_address.version != 4:
            return self.async_abort(reason="not_ipv4")
        host = discovery_info.host
        mac = str(discovery_info.properties.get("mac", "")).lower()
        if mac:
            await self.async_set_unique_id(mac)
            self._abort_if_unique_id_configured(updates={CONF_HOST: host})
        try:
            info = await DongleClient(async_get_clientsession(self.hass), host, "").get_info()
        except DongleConnectionError:
            return self.async_abort(reason="cannot_connect")
        await self.async_set_unique_id(info.mac)
        self._abort_if_unique_id_configured(updates={CONF_HOST: host})
        self._host, self._info = host, info
        self.context["title_placeholders"] = {"name": info.name}
        return await self.async_step_zeroconf_confirm()

    async def async_step_zeroconf_confirm(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        assert self._host and self._info
        errors: dict[str, str] = {}
        if user_input is not None:
            info, error = await self._probe(self._host, user_input[CONF_PASSWORD])
            if error:
                errors["base"] = error
            else:
                return self.async_create_entry(title=info.name, data={CONF_HOST: self._host, CONF_PASSWORD: user_input[CONF_PASSWORD]})
        return self.async_show_form(
            step_id="zeroconf_confirm",
            data_schema=PASSWORD_SCHEMA,
            description_placeholders={"name": self._info.name, "host": self._host},
            errors=errors,
        )

    async def async_step_reauth(self, entry_data: Mapping[str, Any]) -> ConfigFlowResult:
        return await self.async_step_reauth_confirm()

    async def async_step_reauth_confirm(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        entry = self._get_reauth_entry()
        errors: dict[str, str] = {}
        if user_input is not None:
            _, error = await self._probe(entry.data[CONF_HOST], user_input[CONF_PASSWORD])
            if error:
                errors["base"] = error
            else:
                return self.async_update_reload_and_abort(entry, data_updates={CONF_PASSWORD: user_input[CONF_PASSWORD]})
        return self.async_show_form(
            step_id="reauth_confirm",
            data_schema=PASSWORD_SCHEMA,
            description_placeholders={"name": entry.title, "host": entry.data[CONF_HOST]},
            errors=errors,
        )
