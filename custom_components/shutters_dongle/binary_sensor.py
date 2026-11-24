from __future__ import annotations

from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import DongleConfigEntry
from .entity import DongleDeviceEntity


async def async_setup_entry(hass: HomeAssistant, entry: DongleConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback) -> None:
    async_add_entities([DongleRadioProblem(entry.runtime_data)])


class DongleRadioProblem(DongleDeviceEntity, BinarySensorEntity):
    _attr_device_class = BinarySensorDeviceClass.PROBLEM
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator) -> None:
        super().__init__(coordinator, "radio")

    @property
    def is_on(self) -> bool:
        return not self.coordinator.data.radio_ok
