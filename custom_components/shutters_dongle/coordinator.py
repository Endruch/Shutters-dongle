from __future__ import annotations

import logging

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryAuthFailed
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import DongleAuthError, DongleClient, DongleError, DongleInfo, DongleState
from .const import DOMAIN, SCAN_INTERVAL

_LOGGER = logging.getLogger(__name__)


def _layout(state: DongleState) -> set[tuple[int, str, int]]:
    return {(c.ch, c.name, c.proto) for c in state.channels.values()}


class DongleCoordinator(DataUpdateCoordinator[DongleState]):
    config_entry: ConfigEntry

    def __init__(self, hass: HomeAssistant, entry: ConfigEntry, client: DongleClient, info: DongleInfo) -> None:
        super().__init__(hass, _LOGGER, config_entry=entry, name=f"{DOMAIN} {info.name}", update_interval=SCAN_INTERVAL)
        self.client = client
        self.info = info

    async def _async_update_data(self) -> DongleState:
        try:
            state = await self.client.get_state()
        except DongleAuthError as err:
            raise ConfigEntryAuthFailed(str(err)) from err
        except DongleError as err:
            raise UpdateFailed(str(err)) from err
        if self.data is not None and _layout(state) != _layout(self.data):
            _LOGGER.debug("%s: channel layout changed, reloading", self.info.name)
            self.hass.config_entries.async_schedule_reload(self.config_entry.entry_id)
        return state
