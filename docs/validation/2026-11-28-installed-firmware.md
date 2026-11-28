# Reading the installed radiator firmware

The ESP32-S3 bridge read the Firmware Revision String (GATT characteristic
`0x2A26`) from one directly enrolled Shelly BLU TRV and published the complete
build ID on its existing Matter endpoint. The read ran after the regular
telemetry read on an encrypted, bonded connection. Shelly documents the value
as the firmware build ID. [Shelly BLE documentation](https://shelly-api-docs.shelly.cloud/docs-ble/common/#common-gatt-services-and-characteristics) · [Bluetooth Assigned Numbers](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/index-en.html)

| Observation | Result |
| --- | --- |
| Radiator firmware build returned by BLE | `20260728-102342/v1.5.0@aa8644cc`, matching the earlier vendor gateway inventory |
| Matter path | Endpoint 11, Bridged Device Basic Information (0x0039), SoftwareVersionString (0x000A) |
| USB reconnect including telemetry and firmware | 4.220 seconds, encrypted and bonded, firmware read result 0 |
| Radiator settings read in the same transaction | Opening 11%, target 20.0°C, regulator disabled, zero errors |
| Home Assistant display on the radiator device | Awaiting operator confirmation |
| S3 application SHA-256 | `c0a3a2177717834dca8926dbeb2c8a2059bf6c37a2b6599d4360bf6586d5f95b` |

On boot, the first successful telemetry poll attempts the firmware read. The
bridge refreshes it after six hours on the next successful poll. An unsuccessful
attempt can retry after one hour. An explicit USB reconnect requests a fresh
read immediately. The bridge publishes the optional Matter attribute only after
a complete, validated read for that enrollment.

This test covers the installed version. Update discovery, installation, progress
reporting and recovery remain open. [TRV update investigation](../updates.md)
