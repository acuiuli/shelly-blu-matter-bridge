# Prototype build and use

Use the commands in the [README](../README.md). The digest locks ESP-IDF 5.5.5,
ESP-Matter release/v1.5, connectedhomeip, OpenThread and NimBLE to the revisions in
[the lock](../tools/toolchain.lock.json). Compilation does not establish transport
feasibility. Test credentials are supplied by the SDK example providers; these images
are development artifacts. Keep commissioning payloads and serial logs private.

## Prepare the H2 radio

Build the RCP from **the same IDF** as the S3 host. In an interactive pinned container:

```sh
bash tools/build_rcp.sh
```

The pinned example uses 460800 baud. The reference overlay explicitly selects H2
RX23/TX24. Verify these against the actual schematic before flashing. Inspect the
board revision and USB labels.
Flash H2 through its own USB port using the generated IDF flash arguments. Do not flash
the factory border-router host application. Record the RCP binary checksum and SDK
revision in your test report. H2 updates remain a deliberate USB operation.

## Flash the S3

After verifying 8 MB flash, use `idf.py -C firmware -p YOUR_S3_PORT flash monitor`
inside the container with the USB serial device passed through, or from an identically
pinned local SDK. `tools/dev.sh` does not forward USB devices; for Docker add an explicit
`--device` mapping in a local invocation. WSL requires USB forwarding as documented by
[Espressif](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html).
Do not use `erase-flash` for normal upgrades. Preserve NVS and `bridge_store`.

The firmware creates four simulated TRVs. No real valve is controlled. Commission it
as described in [commissioning](commissioning.md), then try climate setpoints and
valve percentage commands in Home Assistant. Read reported position independently.
On the S3 USB console (slots are zero-based):

```text
onboarding
diag
sim 0 delay 5000
sim 1 available 0
sim 2 failures 2
sim 3 hold 1
sim 0 manual 25
sim 1 available 1
sim 3 hold 0
```

`delay` is milliseconds; `failures` fails the next N transport transactions; `hold`
leaves physical position unchanged despite a request. `manual` injects reported
position and cancels outstanding position intent. Unavailable telemetry remains
last-known until 30 seconds, then the parent is unreachable and nullable readings
become null. The child availability interpretation in HA remains a validation gate.
`diag` prints numeric Thread role/eligibility and current/minimum heap, without keys.
The explicit `onboarding` command displays a private development pairing code; omit
that output from ordinary logs. `diag` also reports whether Wi-Fi is uninitialized.
The current simulator emits telemetry every 250 ms; it does not model Shelly's real
advertisement timing, radio interference or battery consumption.

## Limitations

Real discovery, enrollment, GATT/BTHome decoding, bonding, calibration, external
sensor input and TRV updates are not implemented. NimBLE central/observer support
is configured, but persistent scanning after commissioning must still be proved.
USB currently supports simulator configuration and diagnostics; secure real-valve
enrollment is a subsequent milestone. Default development credentials, security
hardening, full conformance testing, and real OTA rollback qualification remain open.
