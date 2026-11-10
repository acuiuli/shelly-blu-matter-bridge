# SPDX-License-Identifier: Apache-2.0
"""Check resolved Kconfig, rather than trusting potentially ignored defaults."""
from pathlib import Path

lines = set(Path("firmware/sdkconfig").read_text().splitlines())
required = ["OPENTHREAD_FTD", "OPENTHREAD_RADIO_SPINEL_UART", "ENABLE_MATTER_OVER_THREAD",
            "THREAD_NETWORK_COMMISSIONING_DRIVER", "ENABLE_OTA_REQUESTOR",
            "BOOTLOADER_APP_ROLLBACK_ENABLE", "BT_NIMBLE_ROLE_CENTRAL", "BT_NIMBLE_ROLE_OBSERVER"]
for name in required:
    if f"CONFIG_{name}=y" not in lines:
        raise SystemExit(f"Required setting missing: {name}")
for name in ["ENABLE_WIFI_STATION", "ENABLE_WIFI_AP", "OPENTHREAD_BORDER_ROUTER",
             "USE_BLE_ONLY_FOR_COMMISSIONING", "ESP_INSIGHTS_ENABLED", "ENABLE_WIFI_TELEMETRY"]:
    if f"CONFIG_{name}=y" in lines:
        raise SystemExit(f"Forbidden setting enabled: {name}")
print("Resolved transport, BLE and OTA configuration passed")
