# Isolating an installed radiator for direct BLE enrollment

One installed BLU TRV on firmware 1.5.0 was enrolled with the experimental
bridge and the original Shelly gateway. The test used the physical valve on
Matter endpoint 10. The bridge ran the S3 [confirmation firmware](2026-11-26-matter-position.md#confirmation-when-the-valve-already-reached-the-requested-position).

## Close with the Shelly gateway still connected

After a bridge restart, the S3 recovered its bond and endpoint and measured
16%. Its RAM command history was empty. The operator sent one Close from Home
Assistant to the Matter valve. The bridge retained the following transaction.

| Measurement | Result |
| --- | --- |
| Last published / fresh bonded preflight / requested level | 16% / 16% / 0% |
| GATT position writes submitted / acknowledged | 1 / 1 |
| First bonded position read | 0% |
| Second bonded read, scheduled 12 seconds later | 16% |
| Transaction duration | 19,451 ms |
| Final Matter endpoint report | Reachable, 16% |

The command reached the selected TRV and it reported zero once. The S3 returned
a mismatch for the delayed read. The cause of reopening was undetermined.
This run failed sustained closure.

## Removing only the selected association

The operator selected a test window for this radiator. The gateway and valve
identities were matched to the private installation record. A local recovery
snapshot captured all three gateway valve configurations and the selected
TRV's remote settings, status and schedule list. The live position was 16%,
the setpoint was 4°C and the minimum opening was 0%. Automatic regulation was
disabled. The valve reported no errors, active boost, override or onboard schedules.

The vendor gateway accepted one `BluTrv.Delete` for the verified component.
Reading the gateway's dynamic component directory afterwards confirmed it
was absent and both other valve configurations matched the saved values.
The Shelly gateway continued serving the other radiators. [Vendor component removal](https://shelly-api-docs.shelly.cloud/gen2/Devices/Gen3/ShellyBluGwG3/#blutrvdelete-example)

After removing the association, a new encrypted reconnect from the bridge using
its existing bond completed in 1,816 ms. The TRV reported 16%, a 4°C target,
successful calibration, a functions mask of 160 and zero errors. Endpoint 10
remained reachable. This step verifies independent BLE access with the original
gateway absent for this radiator. Establishing new link keys requires the
valve's physical BLE pairing window.
