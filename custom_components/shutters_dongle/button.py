from __future__ import annotations

from collections.abc import Awaitable, Callable

from homeassistant.components.button import ButtonDeviceClass, ButtonEntity
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import DongleConfigEntry
from .api import DongleError
from .const import PROTO_SOMFY
from .entity import DongleChannelEntity, DongleDeviceEntity


async def async_setup_entry(hass: HomeAssistant, entry: DongleConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback) -> None:
    coordinator = entry.runtime_data
    client = coordinator.client
    entities: list[ButtonEntity] = [
        DongleActionButton(coordinator, "restart", client.reboot, ButtonDeviceClass.RESTART),
        DongleActionButton(coordinator, "access_point", client.access_point),
    ]
    for c in coordinator.data.channels.values():
        if c.proto == PROTO_SOMFY:
            entities.append(DongleCommandButton(coordinator, c.ch, "shade", "SHADE"))
        entities.append(DongleCommandButton(coordinator, c.ch, "learn", "LEARN"))
    async_add_entities(entities)


class DongleCommandButton(DongleChannelEntity, ButtonEntity):
    def __init__(self, coordinator, ch: int, key: str, cmd: str) -> None:
        super().__init__(coordinator, ch)
        self._cmd = cmd
        self._attr_translation_key = key
        self._attr_unique_id = f"{coordinator.info.mac}_{ch}_{key}"
        if key == "learn":
            # Pairing is a one-off job; keep it out of dashboards unless the user turns it on.
            self._attr_entity_category = EntityCategory.CONFIG
            self._attr_entity_registry_enabled_default = False

    async def async_press(self) -> None:
        await self._send(self._cmd)


class DongleActionButton(DongleDeviceEntity, ButtonEntity):
    _attr_entity_category = EntityCategory.CONFIG

    def __init__(
        self,
        coordinator,
        key: str,
        action: Callable[[], Awaitable[None]],
        device_class: ButtonDeviceClass | None = None,
    ) -> None:
        super().__init__(coordinator, key)
        self._action = action
        self._attr_device_class = device_class

    async def async_press(self) -> None:
        try:
            await self._action()
        except DongleError as err:
            raise HomeAssistantError(f"{self.coordinator.info.name}: {err}") from err
