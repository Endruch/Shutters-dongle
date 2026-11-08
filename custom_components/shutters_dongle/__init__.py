from __future__ import annotations

import logging

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST, CONF_PASSWORD, Platform
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryAuthFailed, ConfigEntryNotReady
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import DongleClient, DongleError
from .const import DOMAIN
from .coordinator import DongleCoordinator

_LOGGER = logging.getLogger(__name__)

PLATFORMS = [Platform.BUTTON, Platform.COVER, Platform.UPDATE]

type DongleConfigEntry = ConfigEntry[DongleCoordinator]


async def async_setup_entry(hass: HomeAssistant, entry: DongleConfigEntry) -> bool:
    host = entry.data[CONF_HOST]
    client = DongleClient(async_get_clientsession(hass), host, entry.data[CONF_PASSWORD])
    try:
        info = await client.get_info()
    except DongleError as err:
        raise ConfigEntryNotReady(f"{host}: {err}") from err
    if entry.unique_id and info.mac != entry.unique_id:
        raise ConfigEntryNotReady(f"{host} is now a different dongle ({info.mac})")
    if not info.pw_set:
        raise ConfigEntryAuthFailed("the dongle's password has been reset")

    coordinator = DongleCoordinator(hass, entry, client, info)
    await coordinator.async_config_entry_first_refresh()
    try:
        await client.link()
    except DongleError as err:
        _LOGGER.warning("%s: couldn't switch off MQTT discovery on the dongle (%s); untick it on its System page to avoid duplicate covers", info.name, err)
    entry.runtime_data = coordinator

    reg = dr.async_get(hass)
    reg.async_get_or_create(
        config_entry_id=entry.entry_id,
        identifiers={(DOMAIN, info.mac)},
        connections={(dr.CONNECTION_NETWORK_MAC, dr.format_mac(info.mac))},
        name=info.name,
        manufacturer="DIY",
        model=info.model,
        sw_version=info.fw,
        configuration_url=f"http://{host}",
    )
    live = _live_identifiers(coordinator)
    for device in dr.async_entries_for_config_entry(reg, entry.entry_id):
        if not device.identifiers & live:
            reg.async_update_device(device.id, remove_config_entry_id=entry.entry_id)

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: DongleConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


async def async_remove_config_entry_device(hass: HomeAssistant, entry: DongleConfigEntry, device: dr.DeviceEntry) -> bool:
    return not device.identifiers & _live_identifiers(entry.runtime_data)


def _live_identifiers(coordinator: DongleCoordinator) -> set[tuple[str, str]]:
    mac = coordinator.info.mac
    return {(DOMAIN, mac)} | {(DOMAIN, f"{mac}_{ch}") for ch in coordinator.data.channels}
