"""BLE GATT changes from a client, the way the app applies them.

Usage: <venv with bleak + pyserial> ble_gatt_apply_test.py
Stages a service + characteristic, checks the link still works, applies
(`packet_settings_ble_apply_t`), reconnects and finds the service; then the
same for its removal. Windows drops every GATT object of the device on the
Service Changed an apply causes, so the client reconnects after it (the
board applies 200 ms after answering, SYS_BLE.MD).
"""
from __future__ import annotations

import sys
import time

from runit_link import BLE_LOGS, BLE_TX, BleLink

SVC, CHR = 0xFF30, 0xFF31
fails = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global fails
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1


def main() -> int:
    link = BleLink()

    def ok(packet: str, **values) -> bool:
        r = link.call(packet, timeout=4, **values)
        return r is not None and r.ok

    async def reconnect() -> None:
        await link._client.disconnect()
        await link._client.connect()
        await link._client.start_notify(BLE_TX, link._on_notify)
        await link._client.start_notify(BLE_LOGS, link._on_notify)

    def services() -> list[str]:
        return sorted(s.uuid[4:8] for s in link._client.services)

    check("stage service", ok("packet_settings_ble_service_create_t", uuid=SVC, is_primary=1))
    check("stage characteristic", ok("packet_settings_ble_char_create_t", service_uuid=SVC, uuid=CHR, is_write=0, is_indicate=0, is_notify=1, tx_buffer_size=64, rx_buffer_size=0, name="t"))
    check("link works while staged", ok("packet_sys_power_get_status_t"))
    check("apply answered", ok("packet_settings_ble_apply_t"))
    time.sleep(0.6)
    link._run(reconnect(), 30)
    check("service there after reconnect", f"{SVC:04x}" in services(), str(services()))
    check("link works after reconnect", ok("packet_sys_power_get_status_t"))
    check("stage removal", ok("packet_settings_ble_service_remove_t", uuid=SVC))
    check("link works while staged", ok("packet_sys_power_get_status_t"))
    check("apply answered", ok("packet_settings_ble_apply_t"))
    time.sleep(0.6)
    link._run(reconnect(), 30)
    check("service gone after reconnect", f"{SVC:04x}" not in services(), str(services()))
    check("link works after reconnect", ok("packet_sys_power_get_status_t"))
    link.close()
    print(f"\n{'ALL PASS' if fails == 0 else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
