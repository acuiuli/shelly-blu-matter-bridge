# 2026-11-24: SDK upgrade and board bring-up

This report separates software checks from observed hardware behavior. The repository
remains private until a working MVP is validated.

## Software validation

- Latest stable managed versions were audited as described in
  [development versions](../development-versions.md).
- ESP-IDF 6.1.1 and ESP-Matter 1.6.1 cross-builds pass for S3 and H2. The Matter
  const-log-tag and connectedhomeip valve-range patches are recorded and included
  in the inventory with modification notices.
- Five SDK revisions and 82 Python tool versions match their lock.
- CMake 4.4.5, Ninja 1.13.2 and clang-format 23.1.5 run successfully.
- Host reconciliation tests pass; five release-tool tests pass.
- Resolved FTD, UART RCP, BLE central/observer and OTA/rollback checks pass; Matter
  Wi-Fi roles and commissioning-only BLE are disabled.
- A forced registry dependency update retains five current SDK-compatible packages.
- Dependency collection records five registry components, 175 linked archives and
  457 discovered license/notice files. All archive hashes resolve. Legal review remains
  incomplete; this is not a distributable firmware release.
- Documentation checks, shell/Python syntax and workflow YAML parsing pass.

## Board identification and recovery preparation

The user identified the PCB as **ESP Thread Border Router/Zigbee Gateway v1.2** and
confirmed both modules. USB device discovery and esptool 5.7.0 observed:

| Interface | Chip | Observed memory |
| --- | --- | --- |
| USB2 / COM5 in this Windows session | ESP32-S3, revision 0.2 | 16 MB flash; 8 MB embedded PSRAM reported by chip identification |
| USB1 / COM6 in this Windows session | ESP32-H2FH4S, revision 1.2 | 4 MB flash |

COM assignments are session-specific. PSRAM capacity is chip-identification data,
not a runtime memory test. The default portable build still uses an 8 MB S3 layout
and 2 MB H2 image header, which fit these larger chips. Upper flash capacity is unused
in this initial profile. PSRAM allocation has not been enabled or qualified.

Both complete factory images were read in 1 MiB chunks, assembled and SHA-256 checked
under ignored `.private/hardware-2026-11-24/`. The first monolithic S3 read stopped
with a serial-stream error; the bounded reads completed. Private dumps, identities
and raw logs must not be added to Git or release artifacts.

The H2 was flashed with the matched IDF 6.1.1 RCP and read-back verification passed.
The S3 was held in download mode so its factory host could not restore an older RCP.
The S3 application was subsequently flashed and read-back verification passed. Its
new project data area was initialized once after checking the full factory backup.
A USB software reset initially left it in ROM download mode (`boot:0x0`). The user
pressed physical S3 RESET with BOOT released; subsequent firmware updates booted
the application through software reset.

## Runtime finding and correction

The first application boot initialized Thread and BLE but then aborted on a generic
write to endpoint 3, Valve Configuration and Control CurrentLevel. ESP-Matter 1.6.1
uses a native cluster server for this cluster; those writes return
`ESP_ERR_NOT_SUPPORTED`, and the old generic command callback is bypassed.

The application now supplies a native valve delegate and uses the SDK's public
current-level/state reporting functions. Open/Close requests queue transport intent;
only new reachable telemetry publishes confirmed position. Failure/supersession
cleanup requires a fresh observation and cannot enqueue another transport command.
Parent Reachable marks stale devices; native valve values remain last-known.

Source review also found a missing upper bound in the native percentage validation.
A pinned, notice-bearing SDK patch rejects values above 100 before the command
handler changes state. Local USB diagnostics invoke that same native decoder.
They do not exercise network access control or controller subscriptions.

OTA boot acceptance now waits for one complete publication and BLE synchronization
after model/Thread initialization, covering the startup failure observed here. This
does not establish network health or qualify OTA rollback.

## Pre-commission radio observations

The corrected application remained responsive with all four simulated endpoints
publishing. An initial five-second passive BLE scan completed normally and received
14 advertisements, with RSSI between -99 and -47 dBm. Diagnostics reported:

- Thread role 0 (disabled before commissioning), router eligibility 1.
- Wi-Fi uninitialized; zero publication scheduling rejections.
- Free heap 102,712–102,848 bytes; minimum heap 101,872 bytes during that scan.

These are local pre-commission observations, not Thread attachment, RF coexistence
under Thread traffic, GATT/bonding, real TRV control, or Wi-Fi spectrum measurements.
Commissioning into the existing Thread network, HA grouping and scans after
commissioning remain pending.

## On-board simulator command and fault checks

The final diagnostic smoke sequence passed in **72.34 seconds** on the S3. These
are simulated TRV observations through native cluster command decoding/delegation
and the reconciliation engine, not measured Shelly radio latency or HA subscriptions.

| Check | Observed result |
| --- | --- |
| Slot 0, five-second injected delay, Open to 60 | Early requested 60 / reported 0, sent; later reported 60, confirmed |
| Native Open to 101 and 255 | Non-success responses; previous accepted intent unchanged |
| Slot 1, position held, Open to 80 | Reported 0; command expired without fabricated motion |
| Slot 2, three transport failures | Reported 0; bounded retries ended in failed |
| Slot 3 absent | Command failed; parent unreachable with telemetry age 35,968 ms |
| Slot 3 returns | Reachable again, reported 0; failed request to 80 was not replayed |
| Slot 0, pending request to 90 then manual position 25 | Pending request superseded; reported 25 after the delayed request would have completed |
| Restore faults and Close all four | All reachable, reported 0 and confirmed |
| Passive BLE scan during the fault sequence | Ten seconds, 21 advertisements, normal completion; RSSI -99 to -41 dBm |
| Memory and scheduling during the sequence | Free heap 102,192–102,248 bytes; minimum 100,632 bytes; zero publication scheduling rejections |

No abort, assertion, panic or brownout was observed during the sequence. Raw captures
and structured snapshots remain private. Repeat using the documented USB `sim`,
`sim_valve`, `diag` and `ble_scan` commands; command outcome numbers are documented
in the [prototype guide](../prototype.md).

The verified S3 application is 1,590,688 bytes, SHA-256
`e19850f6e0b757e5676e30fe77ced2021e6a3bd2ec800347bd54ecc763b0a146`.
Its dependency evidence is in the checked-in inventory and locally generated
`build/dependency-review-native-valve-20261124/`. No distributable release is claimed.

The 12 KiB `bridge_store` partition was read privately before and after an
application reboot and compared byte-for-byte: identical. This validates retention
of the existing identity store across that reboot; endpoint identity through
commissioning changes and OTA remains a separate acceptance test. The private
commissioning code was saved locally for the user's phone-assisted pairing step.

The [official board pin allocation](https://docs.espressif.com/projects/esp-thread-br/en/latest/hardware_platforms.html#host-to-rcp-gpio-allocation)
matches S3 TX18/RX17 to H2 RX23/TX24 at 460800 baud. The received memory configuration
differs from the older documentation's 8 MB flash/2 MB PSRAM description; trust the
measured unit when selecting flash settings.
