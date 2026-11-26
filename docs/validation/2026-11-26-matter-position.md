# First physical valve position on Matter

On 2026-11-26 the commissioned S3 bridge read a 1.5.0 BLU TRV over its saved BLE
bond and published its observed 0% position to a dedicated valve endpoint. The
original Shelly gateway and the installed heating configuration stayed in service.
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

Only the S3 application partition was flashed, after checking the installed partition
table and OTA selection. Flash readback verified the 1,601,344-byte application.
Its SHA-256 is `5ee390dc097a732c58b73dd1e82ffb4b5c46a299f7e9de8698fbcf6c48105183`.

## Reproduction and outstanding checks

On an already commissioned bridge with one verified valve, run `diag` and
`trv_reconnect status`. The former shows a separate `Physical TRV` line. The latter
shows the automatic `position` operation, its security status and which characteristic
was read. Restart the S3 and check for the same endpoint and a fresh sample.

In Home Assistant, open the Matter integration and inspect the new valve's
`current_position`. The default label for slot 0 is **Shelly BLU TRV 1**. Controller
discovery and the displayed value await confirmation. The existing Shelly integration
can expose a second entity for the same physical radiator.

Host tests cover a fresh zero, expiry at the threshold, a renewed sample, invalid
percentages, source mismatch and a clock earlier than the observation. An absent
physical valve, HA's behavior upon expiry, a nonzero reading after movement, clean
onboarding and operation with the original gateway offline still need hardware tests.
The physical valve rejects movement commands in this build. [Bring-up procedure](../real-trv-bringup.md)
