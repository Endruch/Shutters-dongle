from __future__ import annotations

from homeassistant.components.button import ButtonEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import DongleConfigEntry
from .const import PROTO_SOMFY
from .entity import DongleChannelEntity


async def async_setup_entry(hass: HomeAssistant, entry: DongleConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback) -> None:
    coordinator = entry.runtime_data
    async_add_entities(
        DongleShadeButton(coordinator, c.ch) for c in coordinator.data.channels.values() if c.proto == PROTO_SOMFY
    )


class DongleShadeButton(DongleChannelEntity, ButtonEntity):
    _attr_translation_key = "shade"

    def __init__(self, coordinator, ch: int) -> None:
        super().__init__(coordinator, ch)
        self._attr_unique_id = f"{coordinator.info.mac}_{ch}_shade"

    async def async_press(self) -> None:
        await self._send("SHADE")
