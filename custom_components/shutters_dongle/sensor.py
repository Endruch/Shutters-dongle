from __future__ import annotations

from homeassistant.components.sensor import SensorDeviceClass, SensorEntity, SensorStateClass
from homeassistant.const import SIGNAL_STRENGTH_DECIBELS_MILLIWATT, EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import DongleConfigEntry
from .entity import DongleChannelEntity, DongleDeviceEntity


async def async_setup_entry(hass: HomeAssistant, entry: DongleConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback) -> None:
    coordinator = entry.runtime_data
    entities: list[SensorEntity] = [DongleSignalSensor(coordinator), DongleIpSensor(coordinator)]
    for ch in coordinator.data.channels:
        entities.append(DongleRemoteIdSensor(coordinator, ch))
        entities.append(DongleRollingCodeSensor(coordinator, ch))
    async_add_entities(entities)


class DongleSignalSensor(DongleDeviceEntity, SensorEntity):
    _attr_device_class = SensorDeviceClass.SIGNAL_STRENGTH
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_native_unit_of_measurement = SIGNAL_STRENGTH_DECIBELS_MILLIWATT
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator) -> None:
        super().__init__(coordinator, "wifi_signal")

    @property
    def native_value(self) -> int | None:
        return self.coordinator.data.rssi


class DongleIpSensor(DongleDeviceEntity, SensorEntity):
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator) -> None:
        super().__init__(coordinator, "ip_address")

    @property
    def native_value(self) -> str | None:
        return self.coordinator.data.ip


class _ChannelDiagnostic(DongleChannelEntity, SensorEntity):
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator, ch: int, key: str) -> None:
        super().__init__(coordinator, ch)
        self._attr_translation_key = key
        self._attr_unique_id = f"{coordinator.info.mac}_{ch}_{key}"

    @property
    def available(self) -> bool:
        # Settings stay readable even while the radio is down.
        return self.coordinator.last_update_success and self.channel is not None


class DongleRemoteIdSensor(_ChannelDiagnostic):
    def __init__(self, coordinator, ch: int) -> None:
        super().__init__(coordinator, ch, "remote_id")

    @property
    def native_value(self) -> str | None:
        return self.channel.remote_id if self.channel else None


class DongleRollingCodeSensor(_ChannelDiagnostic):
    def __init__(self, coordinator, ch: int) -> None:
        super().__init__(coordinator, ch, "rolling_code")

    @property
    def native_value(self) -> int | None:
        return self.channel.code if self.channel else None
