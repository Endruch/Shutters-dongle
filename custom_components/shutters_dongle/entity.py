from __future__ import annotations

from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .api import Channel, DongleError
from .const import DOMAIN, PROTO_SOMFY
from .coordinator import DongleCoordinator


class DongleChannelEntity(CoordinatorEntity[DongleCoordinator]):
    _attr_has_entity_name = True

    def __init__(self, coordinator: DongleCoordinator, ch: int) -> None:
        super().__init__(coordinator)
        self.ch = ch
        mac = coordinator.info.mac
        channel = coordinator.data.channels[ch]
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, f"{mac}_{ch}")},
            name=channel.name,
            manufacturer="DIY",
            model=f"{'Somfy RTS' if channel.proto == PROTO_SOMFY else 'A-OK'} channel {ch}",
            via_device=(DOMAIN, mac),
        )

    @property
    def channel(self) -> Channel | None:
        return self.coordinator.data.channels.get(self.ch)

    @property
    def available(self) -> bool:
        return super().available and self.coordinator.data.radio_ok and self.channel is not None

    async def _send(self, cmd: str) -> None:
        try:
            await self.coordinator.client.send_command(self.ch, cmd)
        except DongleError as err:
            raise HomeAssistantError(f"{self.coordinator.info.name}: {cmd} failed: {err}") from err
        await self.coordinator.async_request_refresh()
