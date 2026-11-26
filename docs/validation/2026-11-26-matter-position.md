# First physical valve position on Matter

On 2026-11-26 Home Assistant discovered the new physical Matter valve on the
already-commissioned bridge. The operator confirmed that the new **Shelly BLU TRV 1**
valve entity displayed a `current_position` of **0%**. The S3 obtained that position
from an installed BLU TRV on firmware 1.5.0 using its saved BLE bond. The original
Shelly gateway and the installed heating configuration stayed in service.
The H2 continued running the previously validated RCP image.

## Board observations

| Observation | First boot after application update | After a second S3 hardware reset |
| --- | --- | --- |
| Physical Matter endpoint and USB enrollment slot | 10, slot 0 | 10, slot 0 |
| BLE security | Encrypted and bonded, Just Works | Encrypted and bonded, Just Works |
| Automatic position-only GATT transaction | 0%, 6,420 ms | 0%, 1,519 ms |
| Physical endpoint after publication | Reachable, 0%, sample age 5,226 ms | Reachable, 0%, sample age 1,178 ms |
| Network | Thread router, router eligible, Wi-Fi uninitialized | Thread router, router eligible, Wi-Fi uninitialized |
| Free heap / minimum observed, bytes | 97,144 / 92,976 | 99,092 / 95,780 |

The USB diagnostics record the percentage after the native Matter server accepts
CurrentLevel and CurrentState and the bridge publishes Reachable. The BLE operation
read only the position characteristic. Both snapshots showed four simulator slots in
addition to the physical endpoint, and zero blocked physical commands. The board
retained its commissioned fabrics. No position, setpoint or regulator write was
issued in either test.

At 311,773 ms of uptime, a third check found another automatic position read
completed in 1,823 ms. The sample was 6,978 ms old, reachable and still at 0%.
The bridge was still a Thread router. No manual refresh was requested.

Only the S3 application partition was flashed, after checking the installed partition
table and OTA selection. Flash readback verified the 1,601,344-byte application.
Its SHA-256 is `5ee390dc097a732c58b73dd1e82ffb4b5c46a299f7e9de8698fbcf6c48105183`.

## Position feedback during a Shelly-commanded movement

The operator saw Better Thermostat displaying idle during this test. Before sending a movement command, both
gateways measured an opening of 18%. The test used that live value for restoration.
The Shelly gateway sent all movement commands. The S3 read the valve over its own
BLE bond and updated Matter endpoint 10. The operator saw the sequence in Home
Assistant on **Shelly BLU TRV 1**.

| Stage | Matter position after fresh BLE read | Shelly `TRV.GetStatus.pos` | BLE read duration |
| --- | --- | --- | --- |
| Live baseline | 18% | 18% | 9,018 ms |
| Open to 20% | 20% | 20% | 1,977 ms |
| Close to 0% | 0% | 18% two seconds later | 1,981 ms |
| Restore the original position | 18% | 18% | 1,654 ms |

The S3 completed an encrypted reconnect and five reads per observation. The
duration above measures that transaction. The first closure check ended when the
Shelly status disagreed with the bridge. Its 18% reading preceded the restore
command. The setpoint stayed at 4°C, automatic temperature control remained off,
and the valve reported successful calibration and no errors.

On a second closure check, the test sent one close command and no restore command.
Shelly reported 0% at 17:03:29 CET and the S3 reported 0% at 17:03:33. At
17:03:38 Shelly reported 18%, followed by an encrypted S3 read of 18% at
17:03:41. The test stopped on the live starting position. The two paths therefore
confirmed a transient closure. A single zero reading is insufficient evidence of
sustained closure for the boiler lease.

A read-only check at 17:04:44 CET still found 18%, a 4°C target, minimum opening
0%, automatic regulation off and no errors. The valve listed zero onboard schedule
rules and reported no active boost or temperature override.

Home Assistant action history subsequently showed a
`number.set_value` request of 18 on the original Shelly valve-opening entity at
17:03:34 CET, one second after the S3 zero and four seconds before Shelly reported
18%. HA identifies the action as “Via Number” and offered no trace. This confirms
the outgoing HA request. Better Thermostat's Beta2 Shelly adapter uses that action,
so it is a possible initiator. The available action context does not identify the
upstream caller. [Better Thermostat Shelly adapter](https://github.com/KartoffelToby/better_thermostat/blob/2.0.0-Beta2/custom_components/better_thermostat/adapters/shelly.py)
and [HA action history](https://www.home-assistant.io/docs/tools/dev-tools/#action-history-tab).

The original Shelly integration and boiler controls were in service throughout. [Shelly TRV status and configuration fields](https://shelly-api-docs.shelly.cloud/docs-ble/Devices/BLU_ZB/trv/#trvgetstatus)

## Direct USB command checks

A subsequent S3 application build added the local `trv_position` command.
Flash readback verified the 1,603,312-byte image with SHA-256
`65065c01f54258658557fb126175feb8e1aff81c9d56bb89781a4c216b1a9c65`. The board recovered its existing fabric, all four simulator slots
and physical endpoint 10.

| USB check | Board result |
| --- | --- |
| Out-of-range percentage | Rejected before queuing |
| Empty enrollment slot | Rejected before a bonded read or write |
| Starting position differs from the encrypted read | Rejected, zero writes submitted |
| Expected and requested position both 0% | Zero writes submitted. Initial and delayed reads both returned 0%, completing in 18,856 ms |
| Retrieve the result during another BLE operation | Previous USB command result and both readbacks still available |

Host tests covered the write decision, request expiry, reuse of consumed intent,
an enabled or unread regulator, missing telemetry, calibration and fault status.
The tests on the installed valve exercised the guards and both encrypted readbacks.
These guard checks preceded the first S3 actuation test below.
[USB procedure](../real-trv-bringup.md#7-test-a-direct-usb-position-command)

## Reproduction and outstanding checks

On an already commissioned bridge with one verified valve, run `diag` and
`trv_reconnect status`. The former shows a separate `Physical TRV` line. The latter
shows the automatic `position` operation, its security status and which characteristic
was read. Restart the S3 and check for the same endpoint and a fresh sample.

In Home Assistant, open the Matter integration and inspect the new valve's
`current_position`. The default label for slot 0 is **Shelly BLU TRV 1**. Select
the entity under Matter when the Shelly integration also exposes the radiator.

Host tests cover a fresh zero, expiry at the threshold, a renewed sample, invalid
percentages, source mismatch and a clock earlier than the observation. An absent
physical valve, HA's behavior upon expiry, clean onboarding and operation with
the original gateway offline still need hardware tests.
The original observation-only build rejected physical commands. [Bring-up procedure](../real-trv-bringup.md)

## First position write from the S3

The operator switched off the Better Thermostat controlling this radiator. The
vendor gateway remained online. A fresh encrypted reconnect found 0%, a 4°C
setpoint, functions mask 160, successful calibration and zero reported errors.

| USB operation on the S3 | First read | Delayed read | Result |
| --- | --- | --- | --- |
| Request 20% from a measured 0% | 20% | 0% | One acknowledged BLE write. The delayed mismatch failed the command after 20,479 ms. |
| Independent reconnect | 0% | — | Reconfirmed the starting position in 1,712 ms. No restoration write was needed. |

The delayed read was scheduled 12 seconds after the first read. The setpoint,
functions, calibration and errors matched the baseline on the independent
reconnect. These readings establish command delivery and detection of a reversal.
They do not establish sustained opening, motor travel, or the cause of the reversal.

## Physical Matter command firmware

The S3 application is 1,605,456 bytes, SHA-256
`f3d80734483e5a139b65046542e30ec06afff558bd1560e1e343bc68d2730f8c`.
Flash verification passed. After reboot the bridge retained both fabrics, the four
simulated TRVs and physical endpoint 10. It rejoined Thread as a router with Wi-Fi
uninitialized. Its first bonded read returned 0% in 8,515 ms. Free heap was
97,760 bytes and minimum heap was 92,332 bytes at the diagnostic snapshot.

## Home Assistant Open and Close on endpoint 10

The operator used the Matter valve for both actions. Home Assistant displayed
`current_position: 20` with `is_closed: false` after requesting 20%. After
**Close valve** it displayed `current_position: 0` with `is_closed: true`.
Endpoint 10 had counted two completed Matter operations and zero admission rejections.

The retained Close operation took 7,535 ms. Its encrypted, bonded preflight
read found 0%, a 4°C setpoint, functions mask 160, successful calibration and
zero errors. The operation returned code 3 before any write or post-command
readback. The radiator was already at zero when the S3 checked. The cause and
time of the change are unknown. This firmware retained only the most recent
position operation, so the delayed BLE reading for Open could not be recovered.
The HA state transitions passed for this run. A write that moves an open valve
to closed through Matter still requires a separate observation.

### Confirmation when the valve already reached the requested position

The updated S3 application is 1,605,904 bytes, SHA-256
`670cf5c4cc4932f601691c6cc4c2f3f435023f3e9ac4d55efb95a3ea5ef0b0a3`.
Flash verification passed. The S3 rejoined Thread as a router and recovered
endpoint 10 with its existing BLE bond. Its first read returned 0% in 4,579 ms.

The host regression supplies a cached opening of 20%, an absolute Close request,
and a healthy bonded preflight of 0%. The command proceeds to readback without a
write. The tests cover the equivalent already-open case, an unrelated position
change, expiry, reuse, missing fields, regulator/calibration/error checks and a
strict USB request with a mismatched start.

On the installed board, two no-movement USB tests exercised result retention. A
strict request expecting 100% found 0% and failed before submission. A request
expecting and targeting 0% succeeded with initial and delayed readings of 0%
in 20,264 ms. After a subsequent reconnect started, USB history retained both
completed commands with their original expected, requested and preflight values.
