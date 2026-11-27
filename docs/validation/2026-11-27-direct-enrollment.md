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
gateway absent for this radiator.

## Renewing the bond after gateway removal

The operator briefly pressed the TRV's pairing button. The S3 detected the
selected valve advertising pairing mode and issued one same-slot repair.
The valve returned the peer security error `0x0505` (pairing not supported)
after 386 ms. No encrypted connection or GATT read completed. The error gives
no reason for the refusal. [NimBLE return codes](https://github.com/apache/mynewt-nimble/blob/master/docs/ble_hs/ble_hs_return_codes.rst)

Repair had already dropped the S3's previous key for this peer. Slot 0 retained
its pending identity, endpoint 10 reported unreachable and the other three
slots stayed empty. A subsequent stored-key reconnect returned 23
(`BLE_HS_EAUTHEN`) in 36 ms. The Shelly gateway still listed only the two
other radiators, with both configurations matching the saved baseline.

After a second button press, the S3 discovered the selected valve in pairing
mode and ran `trv_enroll` against the existing pending slot. A separate
`trv_reconnect` then used the newly stored bond. Both connections were
encrypted and completed all five GATT reads.

| Operation | Elapsed | Opening | Setpoint | Calibration | Functions mask | Errors |
| --- | ---: | ---: | ---: | --- | ---: | ---: |
| Enroll the same peer in pending slot 0 | 2,622 ms | 0% | 4°C | Complete | 160 | 0 |
| Reconnect with the new bond | 2,243 ms | 0% | 4°C | Complete | 160 | 0 |

Endpoint 10 reported reachable again at 0%. A further check of the vendor
gateway confirmed the selected valve was absent and both other configurations
matched their saved values. The S3 remained a Thread router. Automatic
temperature control was still disabled. Neither enrollment nor reconnect
issued a position command. The last reading before the failed repair had been
16%. The test did not establish when or why it changed.

This verifies fresh direct bonding for an installed valve on firmware 1.5.0
after its Shelly gateway association was removed. The bridge already had a
reserved identity for this valve. First enrollment from an empty slot with a
factory-new valve and switching its default regulator off remain untested.

## Home Assistant movement after direct enrollment

With the Shelly association still removed, the operator used the existing
Matter valve to set 20% and then Close. The S3 recorded both commands on
endpoint 10. Each started from the expected position and submitted exactly one
acknowledged write over an encrypted, bonded link.

| HA command | Confirmed preflight | First read | Delayed read (12 s) | Transaction duration |
| --- | ---: | ---: | ---: | ---: |
| Set position to 20% | 0% | 20% | 20% | 22,298 ms |
| Close | 20% | 0% | 0% | 18,761 ms |

The operator saw unavailable, then 20% after opening. During Close the entity
displayed unavailable, then closing, then 0%. A separate encrypted reconnect
started more than 30 seconds after the completed Close and read 0% again in
3,727 ms. Calibration was complete, the regulator remained off and there were
zero reported valve errors. These are device-reported positions on one valve.

The S3 firmware used for this run invalidated the preflight observation when
submitting each BLE write. A Matter publish before the first readback could
therefore mark the endpoint unreachable while the command was in progress.

## Availability during movement

The corrected S3 application retains the fresh preflight percentage until it
receives a position readback. The Valve server reports the requested target and
transition separately. A submitted write with missing final confirmation still
expires the sample. The Matter publisher also takes its timestamp after copying
the BLE observation, so a concurrent read cannot appear to come from the future.
USB `diag` counts successful reachable-to-unreachable reports per endpoint.

The S3 image was cross-compiled locally, then written and verified in the
active application partition. Its SHA-256 was
`5d33b84592f27b63644374fdb3c7fd2cb6f19f6a7cec7b8640528fe722f1437b`.
After restart the same endpoint recovered at 0%, with zero completed commands
and zero reachability losses on that boot.

The operator repeated the HA cycle on the corrected image. HA kept the valve
available during both commands and ended at 0%. The S3 recorded the following
encrypted, bonded transactions, each with one submitted and acknowledged write.

| HA command | Preflight | First read | Delayed read (12 s) | Elapsed | Cumulative reachability losses |
| --- | ---: | ---: | ---: | ---: | ---: |
| Set position to 20% | 0% | 20% | 20% | 18,256 ms | 0 |
| Close | 20% | 0% | 0% | 21,899 ms | 0 |

The endpoint finished reachable at 0%, with two completed commands and zero
blocked commands. This run covers one installed valve, two target levels and
successful writes. A disconnected valve during a write, the full position
range and multi-valve operation still require hardware tests.

## Preparing an empty slot for first enrollment

The installed radiator started in verified slot 0, on endpoint 10, at 0%.
The selected valve was still absent from the vendor gateway and the other two
radiators' configurations matched their saved values. The S3 application
with the USB forget command was built locally and verified after flashing.
Its SHA-256 was `72060fb28386100849b27ce76476f1692346e7b8ef3de13e85daa70558f2baa3`.

1. A forget request with a mismatched address was rejected. Slot 0 remained verified.
2. A request for the selected slot and matching address succeeded. All four
   physical slots read empty and endpoint 10 disappeared from bridge diagnostics.
3. After an S3 hardware restart, the endpoint was still absent. The four
   simulated valves and the existing fabric count were preserved, and the S3
   reattached to Thread. A reconnect against empty slot 0 returned
   `BLE_HS_ENOENT` before a BLE connection.

This removal is local to the bridge and can precede or follow the radiator's
physical reset. The next enrollment starts with a freshly generated identity.
