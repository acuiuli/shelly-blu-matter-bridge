# Capability matrix and full-parity blockers

Every row is required for a full-parity release. “Candidate” means the proposed standard
mapping still requires a specification and controller check. **No row has hardware
acceptance evidence yet.** Status of software-only tests is in [results](validation/results.md).
The scope is the function list agreed for the current device page, not every vendor
schedule/display feature. Freeze a sanitized inventory of the installed device page
before parity sign-off; expand this matrix if it reveals another required function.

| Required function | Shelly BLE operation to verify | Legitimate standard Matter candidate | HA representation / acceptance test | Status |
| --- | --- | --- | --- | --- |
| Measured temperature | Validated BTHome temperature | Thermostat.LocalTemperature, °C ×100 | Climate current temperature; manual/device change propagates without synthesis | Simulated |
| Target temperature | Bonded target GATT / TRV.SetTarget | OccupiedHeatingSetpoint | Climate target; readback matches, range/units checked | Simulated |
| Control mode | Enabled-functions flags / vendor configuration | Thermostat.SystemMode only for matching Off/Heat semantics | Climate mode; prove direct-control mode survives commands/restarts; Off must not imply physical closure | Blocked semantics |
| Direct percentage command | Bonded position GATT; automatic control disabled | Valve Configuration and Control Open(targetLevel), Close; Level feature | Valve set position; 0/50/100 applied and confirmed | Simulated; conformance gate |
| Confirmed percentage feedback | Position readback / validated telemetry | CurrentLevel separate from TargetLevel | current_position remains actual during delays/failures | Host tests; hardware pending |
| Battery | Validated BTHome/device telemetry | Power Source.BatPercentRemaining, units 0.5% | Battery sensor; test 0, 50, 100 and unknown | Simulated |
| Availability | Freshness and transport observations | Bridged Device Basic Information.Reachable | Parent and children unavailable after timeout; independently recover | Host tests; HA pending |
| External temperature input | Bonded external-temperature GATT | **Unresolved writable standard interface**; LocalTemperature is reported data, not an external sensor command | Need legitimate controller service and confirmed input effect | **Release blocker** |
| Calibration command | Calibration GATT write | **Unresolved standard command** | Initiate calibration without custom cluster or unrelated command | **Release blocker** |
| Calibration/problem status | Calibration/error readback | ValveFault only for semantically matching faults; calibration status unresolved | Preserve distinct states and faults without inventing meanings | **Release blocker** |
| BLE signal strength | Received radio RSSI | **Unresolved downstream BLE standard diagnostic**; Thread RSSI is a different link | Show per-TRV RSSI with correct provenance | **Release blocker** |
| TRV firmware version | Vendor version/status discovery | Bridged Device Basic Information.SoftwareVersion/String candidate | Distinguish valve version from S3 version | Investigation |
| TRV update availability | Vendor release metadata acquisition | **Unresolved bridged-device update discovery** | Availability matches vendor release for exact model | **Release blocker** |
| TRV update installation | Vendor BLE update protocol | **Unresolved standard exposure**; root OTA Requestor updates the S3 only | Acquire authorized image, install, recover from interruption | **Release blocker** |
| TRV update progress | Vendor transfer/status observation | **Unresolved per-TRV standard progress mapping** | Per-valve progress and final version, including failure/recovery | **Release blocker** |

The [official Shelly TRV protocol](https://shelly-api-docs.shelly.cloud/docs-ble/Devices/BLU_ZB/trv/)
is the source for the BLE investigation. The [HA valve implementation](https://github.com/home-assistant/core/blob/dev/homeassistant/components/matter/valve.py)
shows why cluster presence alone is insufficient: discovery also requires the Water
Valve device type. Pin the HA version in every report. The [ESP-Matter supported types](https://github.com/espressif/esp-matter/blob/256368558724d371737ac0437972207a917f029a/SUPPORTED_DEVICE_TYPES.md)
are implementation support, not proof of conformance for this product composition.

Do not expose external input through a read-only temperature attribute, calibration
through Identify, BLE RSSI through Thread diagnostics, or TRV updates as gateway OTA.
Use standard clusters exclusively. If an unresolved requirement has no legitimate
standard solution, publish the unsupported finding and retain experimental status.
