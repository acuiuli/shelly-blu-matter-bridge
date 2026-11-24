# 2026-11-24: SDK upgrade and board bring-up

This report separates software checks from observed hardware behavior. The repository
remains private until a working MVP is validated.

Current outcome: HA commissioning succeeds after selecting the Thread platform
DNS-SD backend and enabling the controller's test-certificate trust. The user
reports four simulated climates, four valves and battery entities. The board was
elected Thread router; BLE scanning still works after HA commissioning, with
Wi-Fi uninitialized. Real TRV control and full replacement parity remain unvalidated.

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

The S3 application used for the simulator smoke sequence is 1,590,688 bytes, SHA-256
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

## Android discovery follow-up

The user reported "device not found" during initial pairing from Android. The
previous console exposed BLE receiving but not commissioning-window/advertising
state. The cause of that first failure was not captured.

The S3 was updated, without erasing NVS or identity, with local USB
`commissioning status|open` commands. Read-back verification passed. On the updated
image, at 14–16 seconds uptime, hardware reported:

```text
window_open=1 fabrics=0 ble_enabled=1 ble_advertising=1 ble_connections=0
```

Actual NimBLE advertising was active, the pairing code still matched the user's
private file, and `commissioning open` preserved the already-open window. Wi-Fi
remained uninitialized and publication scheduling rejections remained zero.

On retry, Android found the device and displayed an uncertified-device warning
with a Continue / Set up anyway option. The monitor observed a Matter BLE
connection, followed by advertising resuming after disconnect. This establishes
discovery and connection; the user continued past the expected test-attestation
warning. The board then reported one provisional fabric and Thread role 2 (child),
router eligibility 1, Wi-Fi uninitialized, and zero publication scheduling rejections.
Heap at that point was 99,940 bytes, minimum 94,180 bytes.

The phone subsequently reported "cannot reach the device" (Italian UI). The
commissioning window did not close, the fail-safe expired, and the incomplete fabric
and Thread provisioning were rolled back: a later diagnostic showed zero fabrics
and Thread role 0. The post-commission BLE scan was deliberately deferred because
commissioning had not completed. Attachment is not a passed transport gate.

The user confirmed HA OS and an existing Sonoff Dongle Max border router, with
another Matter-over-Thread device already controllable from HA. This gateway remains
an FTD router-eligible node; it does not take over the border-router role.

Source/build review found `CONFIG_USE_MINIMAL_MDNS=y` and `chip_mdns = "minimal"`.
The serial capture contained `Failed to advertise records: 46`; in this SDK,
`0x46` is `CHIP_ERROR_NO_ENDPOINT`. The minimal backend was bypassing the ESP32
platform DNS-SD path that publishes Thread services via OpenThread SRP. The official
[Thread profile](https://github.com/espressif/esp-matter/blob/50c810ccee552cb0f3bfac47ec834ab4aa3c79ea/examples/light/sdkconfig.defaults.c6_thread)
uses `CONFIG_USE_MINIMAL_MDNS=n`.

The project now selects the platform backend explicitly. The resolved-config check
also requires both Thread SRP/DNS clients, rejects minimal mDNS, and verifies the
generated GN backend choice. Runtime acceptance still requires a new phone attempt.

The corrected image cross-build and host/release-tool checks pass. Regression
checks accept the resolved platform configuration and reject the original minimal
mDNS setting, a mismatched GN backend and a disabled SRP client. Read-back flash
verification passed. The identity partition still matches the pre-commission
reference byte-for-byte after the firmware updates and failed pairing attempts.
The private pairing code is unchanged. At 2–4 seconds after the final reboot,
initial pairing advertisements were active with no stored fabric; free heap was
106,872 bytes and minimum heap 105,612 bytes, with Wi-Fi uninitialized.

The corrected SRP image is 1,576,128 bytes, SHA-256
`6e3f68b8470b7ad24195b957572c7b3078365abf034cc1a5cb99be12fca88c9e`.
The current inventory and local evidence in
`build/dependency-review-thread-srp-20261124/` describe this image.

The diagnostic image SHA-256 is
`0684450c1c9943bce74ca775625ea4a248df704c9b9da24ec5da4cf4d88badf2`. Local evidence in
`build/dependency-review-commissioning-20261124/` describes that earlier image. Formatting,
SDK/configuration checks, host reconciliation and all five release-tool tests pass.

## Corrected discovery: router election, controller policy and BLE

With platform DNS-SD/SRP selected, the board retained one Matter fabric, closed its
initial commissioning window and was elected Thread router (role 3). Router eligibility
remained 1. A subsequent window opened during the handover to Home Assistant, and
the phone reported a generic error after the Thread credentials step. Matter Server
logs supplied by the user identify the remaining failure precisely:

```text
OperationalCredentials.DeviceAttestation failed:
This device uses a test/development certificate.
Enable the test-net DCL option of the Matter Server.
```

The controller reached the device using on-network commissioning and rejected its
attestation policy. The logs identify the JavaScript server implementation
(`@matter-server/ws-controller`); the exact installed version has not been recorded.
The user was instructed to enable the official HA OS app option
`enable_test_net_dcl: true`, save and restart Matter Server. Final HA commissioning,
endpoint discovery and grouping are still pending that retry. Android accepting its
own uncertified-device prompt did not enable the HA controller's separate policy.

A ten-second passive BLE scan was then run with the initial fabric stored and the
board operating as Thread router:

| Measurement | Observed result |
| --- | --- |
| Commissioning status before the scan | Window closed, one stored fabric, no BLE connection |
| Thread before/after scan | Role 3 (router), router eligibility 1 |
| BLE host and scan | Synchronized; completed normally; 21 advertisements |
| RSSI bounds | -98 to -50 dBm |
| Free/minimum heap | 105,360–105,368 bytes free; minimum 97,620 bytes |
| Wi-Fi / publication scheduling | Wi-Fi uninitialized; zero scheduling rejections |

No abort, assertion, panic or brownout was observed during this probe. This proves
passive BLE reception alongside an attached Thread router after initial commissioning;
it does not validate GATT/bonding, real Shelly control, mesh forwarding performance,
HA entity presentation or the installation pilot. Raw controller/device captures and
pairing material remain private.

The user confirmed enabling test-net DCL and restarting Matter Server. USB then
opened a new 900-second basic commissioning window successfully while retaining
the existing fabric and Thread router role. Active BLE advertising and the original
private pairing code were reverified. No factory reset or fabric deletion was
performed. The HA retry succeeded as recorded below.

## HA commissioning and entity discovery confirmed

After enabling the controller option, the user confirmed successful setup and
reported all four simulated thermostats, four valve controls and battery readings
present in HA. The suggested gateway name is **BLU TRV Bridge 1**. Device-type
conformance and detailed composition/grouping remain separate acceptance work.

The final hardware probe observed a closed commissioning window, two stored
fabrics, Thread role 3 and router eligibility 1. During another ten-second passive
BLE scan with HA connected, the host remained synchronized and received 23
advertisements with normal completion and RSSI between -96 and -46 dBm. Thread
remained a router before and after the scan. Free heap was 104,976–104,980 bytes,
minimum heap 96,580 bytes; Wi-Fi remained uninitialized and publication scheduling
rejections remained zero. No abort, assertion, panic or brownout was observed.

These observations pass the initial board transport proof and basic HA discovery
check. Real BLE communication, physical actuation, bonded enrollment, complete
maintenance parity, OTA/rollback and the pilot remain unvalidated. The exact HA Core
and installed Matter Server versions still need recording for compatibility qualification.

The user then set a simulated valve to 60% in Home Assistant and confirmed its
reported position settled at 60%. Independent USB diagnostics observed slot 0 with
requested 60, reported 60 and command outcome 3 (confirmed). This verifies one
end-to-end HA percentage command through the native delegate and reconciliation
engine to simulator telemetry. Delayed/failure behavior has passed the local native
command smoke sequence; controller-level failure/availability automation tests and
HA thermostat writes remain separate pending checks.
