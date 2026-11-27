# Temperature and battery from an enrolled radiator

One installed BLU TRV on firmware 1.5.0 was already in valve-only mode on
physical endpoint 11. The original Shelly gateway association had been removed
during the [first-enrollment test](2026-11-27-direct-enrollment.md#first-enrollment-after-a-physical-factory-reset).

During an encrypted reconnect the S3 read the vendor's Sample BTHome Data
characteristic. The 14-byte v2 sample contained a packet ID, 38% battery,
a 20.0°C target, a 21.4°C measured temperature and a status counter. Shelly
defines the order of the repeated temperature objects. The bridge reads the
second object for measured temperature. [Shelly TRV objects](https://shelly-api-docs.shelly.cloud/docs-ble/Devices/BLU_ZB/trv/#bthome-data)

The first integration build published 38% via Power Source but ESP-Matter
returned `ESP_ERR_NOT_SUPPORTED` when the generic attribute update tried to
set Temperature Measurement's MeasuredValue. The corrected build uses the
SDK's native `TemperatureMeasurement::SetMeasuredValue` on the enrolled endpoint.
The S3 application was built locally at 1,611,424 bytes with SHA-256
`cacb2e954cbf08b35c31fd5f30381342f27bc125a9ff940b828348971a043094`.

Host tests covered signed temperatures, valid zero readings, the optional
external sensor, malformed and incomplete samples, packet-ID rollover and stale
data. Only bundled temperature and battery readings from the enrolled peer's
encrypted link can refresh the two sensor values.

After verifying the corrected application on flash, the bridge rejoined
Thread and its automatic poll published 20.5°C and 38% on endpoint 11. The
valve reported 11% open. An independent bonded reconnect completed in 3.506 s,
confirmed a 20.0°C target, 11% opening, regulator disabled (functions mask
160), completed calibration and zero errors. Its sensor read also succeeded.
The Matter attributes still reported 20.5°C and 38% after the reconnect.
At the next scheduled poll, the bridge read and published 20.2°C, 38% battery
and 11% opening. That automatic BLE transaction completed in 2.233 s.

On the existing HA device the operator saw **Batteria** at 38% and a separate
battery status of **Normal**. The percentage comes from the radiator. The SDK
initializes the charge-level status to Normal, and the BLE sample supplied no
charge-level category. The temperature sensor was absent on that first check.
After the operator re-interviewed the parent bridge in HA, the temperature
sensor appeared on the same valve device. The operator confirmed its presence
and did not provide a numeric UI reading. Repeated polling on four valves and
stale readings after a physical disconnect are separate validation steps.
