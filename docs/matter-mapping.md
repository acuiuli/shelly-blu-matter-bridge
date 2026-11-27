# Capability matrix and full-parity blockers

Every row is required for a full-parity release. “Candidate” means the proposed standard
mapping still requires a specification and controller check. Physical position feedback has been observed on an installed TRV in HA, including
a reversal. Actuation has separate acceptance criteria for delivery and a sustained
result. [Physical valve report](validation/2026-11-26-matter-position.md) · [Simulator results](validation/results.md)

The scope is the function list agreed for the current device page, not every vendor
schedule/display feature. Freeze a sanitized inventory of the installed device page
before parity sign-off; expand this matrix if it reveals another required function.

| Required function | Shelly BLE operation to verify | Legitimate standard Matter candidate | HA representation / acceptance test | Status |
| --- | --- | --- | --- | --- |
| Measured temperature | Second temperature object (0x45) in the bonded Sample BTHome Data read | Temperature Measurement.MeasuredValue on the physical valve, in hundredths of a degree Celsius. Thermostat.LocalTemperature serves the same measurement on the physical climate entity | Temperature sensor alongside the enrolled valve. Verify a measured value distinct from the setpoint and unknown on expiry | [The S3 published 20.5°C with a 20.0°C target. HA discovered the entity after a bridge re-interview. Stale sample handling passed host tests](validation/2026-11-27-physical-sensors.md) |
| Target temperature | Bonded target GATT / TRV.SetTarget | OccupiedHeatingSetpoint | Climate target; readback matches, range/units checked | [The bridge exposes the physical heating target with a bonded write and independent readback](validation/2026-11-27-physical-thermostat.md) |
| Control mode | Enabled-functions flags / vendor configuration; explicit USB valve-only setup clears bit 0 with full-mask readback | Thermostat.SystemMode only for matching Off/Heat semantics; no established standard mapping for automatic-regulator enable | Prove valve-only setup without Shelly app, retention through restart/reset handling; climate Off must not imply closure or change the regulator flag | [One factory-reset TRV switched from mask 161 to 160](validation/2026-11-27-direct-enrollment.md#first-enrollment-after-a-physical-factory-reset). Bonded reads confirmed the change before and after S3 restart. The thermostat reports the regulator state and rejects mode changes. TRV battery-cycle retention and writable control-mode semantics remain open |
| Direct percentage command | Bonded position GATT; automatic control disabled | Valve Configuration and Control Open(targetLevel), Close; Level feature | Valve set position; 0/50/100 applied and confirmed | One isolated valve confirmed HA's 0% → 20% → 0% cycle on immediate and 12-second reads. A separate reconnect confirmed closure. A prior Close with the vendor gateway associated reverted to 16%. Full-range and long-duration validation remain open. [Isolation report](validation/2026-11-27-direct-enrollment.md) |
| Confirmed percentage feedback | Position readback / validated telemetry | CurrentLevel separate from TargetLevel | current_position remains actual during delays/failures | [HA displayed the Shelly-commanded position changes. The bridge reported 20%, 0% and restored 18%](validation/2026-11-26-matter-position.md). HA also showed consistent open/closed states and percentages after its own actions |
| Battery | BTHome object 0x01 in whole percent from the same bonded read | Power Source.BatPercentRemaining, units 0.5% | Battery sensor on the valve. Test percentage conversion, valid zero and unknown | [One enrolled radiator returned 38% and HA displayed 38%](validation/2026-11-27-physical-sensors.md) |
| Availability | Freshness and transport observations | Bridged Device Basic Information.Reachable | Parent and children unavailable after timeout; independently recover | [HA kept one isolated physical valve available throughout Set Position and Close](validation/2026-11-27-direct-enrollment.md#availability-during-movement). HA timeout and absent-valve recovery still need hardware validation |
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
