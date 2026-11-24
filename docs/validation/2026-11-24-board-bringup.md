# 2026-11-24: SDK upgrade and board bring-up

This report separates software checks from observed hardware behavior. The repository
remains private until a working MVP is validated.

## Software validation

- Latest stable managed versions were audited as described in
  [development versions](../development-versions.md).
- ESP-IDF 6.1.1 and ESP-Matter 1.6.1 cross-builds pass for S3 and H2. The Matter
  const-log-tag compatibility patch is recorded and included in the inventory.
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
S3 application flashing, runtime Thread/BLE diagnostics and commissioning are pending.

The [official board pin allocation](https://docs.espressif.com/projects/esp-thread-br/en/latest/hardware_platforms.html#host-to-rcp-gpio-allocation)
matches S3 TX18/RX17 to H2 RX23/TX24 at 460800 baud. The received memory configuration
differs from the older documentation's 8 MB flash/2 MB PSRAM description; trust the
measured unit when selecting flash settings.
