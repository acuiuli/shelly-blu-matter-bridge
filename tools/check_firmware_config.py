# SPDX-License-Identifier: Apache-2.0
"""Check resolved Kconfig, rather than trusting potentially ignored defaults."""
from pathlib import Path

lines = set(Path("firmware/sdkconfig").read_text().splitlines())
required = ["OPENTHREAD_FTD", "OPENTHREAD_RADIO_SPINEL_UART", "ENABLE_MATTER_OVER_THREAD",
            "OPENTHREAD_SRP_CLIENT", "OPENTHREAD_DNS_CLIENT",
            "THREAD_NETWORK_COMMISSIONING_DRIVER", "ENABLE_OTA_REQUESTOR",
            "BOOTLOADER_APP_ROLLBACK_ENABLE", "BT_NIMBLE_ROLE_CENTRAL", "BT_NIMBLE_ROLE_OBSERVER",
            "BT_NIMBLE_NVS_PERSIST"]
for name in required:
    if f"CONFIG_{name}=y" not in lines:
        raise SystemExit(f"Required setting missing: {name}")
for name in ["ENABLE_WIFI_STATION", "ENABLE_WIFI_AP", "OPENTHREAD_BORDER_ROUTER",
             "USE_MINIMAL_MDNS", "BT_NIMBLE_SMP_REMOVE_BOND_ON_PAIR_FAIL_AS_CENTRAL",
             "BT_NIMBLE_SMP_UNBOND_ON_KEY_MISSING",
             "USE_BLE_ONLY_FOR_COMMISSIONING", "ESP_INSIGHTS_ENABLED", "ENABLE_WIFI_TELEMETRY"]:
    if f"CONFIG_{name}=y" in lines:
        raise SystemExit(f"Forbidden setting enabled: {name}")
if "CONFIG_BT_NIMBLE_MAX_BONDS=8" not in lines:
    raise SystemExit("Expected eight local bond slots for four TRVs plus BLE commissioning headroom")
args = Path("firmware/build/esp-idf/chip/args.gn").read_text()
if 'chip_mdns = "platform"' not in args:
    raise SystemExit("Thread requires the platform DNS-SD backend for SRP discovery")
print("Resolved transport, Thread DNS-SD/SRP, BLE and OTA configuration passed")
