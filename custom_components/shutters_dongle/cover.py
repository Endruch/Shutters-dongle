from __future__ import annotations

from typing import Any

from homeassistant.components.cover import CoverDeviceClass, CoverEntity, CoverEntityFeature
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import DongleConfigEntry
from .entity import DongleChannelEntity


async def async_setup_entry(hass: HomeAssistant, entry: DongleConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback) -> None:
    coordinator = entry.runtime_data
    async_add_entities(DongleCover(coordinator, ch) for ch in coordinator.data.channels)


class DongleCover(DongleChannelEntity, CoverEntity):
    _attr_name = None
    _attr_device_class = CoverDeviceClass.SHUTTER
    _attr_assumed_state = True
    _attr_supported_features = CoverEntityFeature.OPEN | CoverEntityFeature.CLOSE | CoverEntityFeature.STOP

    def __init__(self, coordinator, ch: int) -> None:
        super().__init__(coordinator, ch)
        self._attr_unique_id = f"{coordinator.info.mac}_{ch}"

    @property
    def _state(self) -> int:
        c = self.channel
        return c.state if c else -1

    @property
    def current_cover_position(self) -> int | None:
        return None if self._state < 0 else 100 - self._state

    @property
    def is_closed(self) -> bool | None:
        return None if self._state < 0 else self._state == 100

    async def async_open_cover(self, **kwargs: Any) -> None:
        await self._send("UP")

    async def async_close_cover(self, **kwargs: Any) -> None:
        await self._send("DOWN")

    async def async_stop_cover(self, **kwargs: Any) -> None:
        await self._send("STOP")
