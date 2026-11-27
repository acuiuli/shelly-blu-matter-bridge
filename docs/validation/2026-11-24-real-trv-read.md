# 2026-11-24: first installed TRV discovery and direct read

This is a read-only transport result, not a real-actuator control release. The
Matter endpoints still represent four simulated devices. The repository was private
during this test.

## Reference and selection

The user selected an installed Shelly BLU TRV on firmware 1.5.0. Its original
Shelly BLU Gateway Gen3 runs `20261009-104928/2.0.2-g34427f1`. The gateway's local
read-only API supplied private baselines for three paired valves. All three
reported firmware 1.5.0, automatic temperature control off and position 0%.

The chosen valve is the original gateway's local component 202, with a baseline
target of 4°C. That component number is not a Matter endpoint or a future slot
assignment. Gateway/valve addresses, household details and raw baseline captures
remain under ignored `.private/` and are not published.

## Discovery

The new `trv_discover` USB command uses bounded active scanning through Matter's
existing NimBLE host. A 15-second hardware scan recognized six model
`SBTR-001AEU` candidates, dropped none, saw no malformed AD frames, and rejected
two unsupported Shelly manufacturer-block layouts. RSSI ranged from -95 to -52 dBm
across the recognized candidates. Addresses and payloads were not printed.

Two stored Matter fabrics survived the application update. Thread remained role 3
(router), router eligibility 1, Wi-Fi uninitialized and zero publication scheduling
rejections. Free heap was 105,808–105,916 bytes; minimum was 100,236 bytes in this run.
This is recognition of advertising metadata, not authenticated identity or enrollment.

## Direct GATT read of the selected valve

The subsequent read-probe image was flashed and read-back verified. A fresh scan
matched the privately selected valve before a single GATT connection. The probe
made no characteristic writes and did not request persistent bonding. The shared
host's bonding-request setting was temporarily cleared on the host task and was
observed restored to its original value 1 after disconnection.

The probe completed in **5,282 ms**, including connection and service discovery:

| Field | Observed result |
| --- | --- |
| Target temperature | 4°C; raw vendor units normalized to 400 centidegrees |
| Reported position | 0% |
| Calibration | 1, calibrated |
| Enabled functions | 160 (`0x00a0`); automatic temperature control bit 0 is clear |
| Errors | Read failed with status 261 (`0x0105`, ATT insufficient authentication); value remains unknown |

The protected read failure was retained as an error and did not create an invented
clear problem status. Thread remained role 3 before/after the connection, Wi-Fi
uninitialized and publication scheduling rejections zero. Free heap was
103,664–104,824 bytes, minimum 99,400 bytes. No abort, panic or brownout was observed.

After the probe, a fresh `TRV.GetStatus` call through the original gateway succeeded
in 5,656 ms using its existing pairing. It reported position 0%, target 4°C and no
errors. This is a separate vendor-gateway observation; it does not turn the new
bridge's authentication-denied error read into an authenticated result.

## Software verification and remaining work

Host CTest passes reconciliation, advertisement identity/bounds/ambiguity handling,
and scalar GATT length/unit/range/bitmask tests. S3 compilation, resolved Thread
DNS-SD/SRP policy and formatting pass. Five release-tool tests pass. The notice
inventory retains five registry components, 175 linked archives and 457 notices;
binary redistribution review remains incomplete.

Persistent enrollment/bonds, authenticated status reads, real telemetry in Matter,
motor commands and recovery are not implemented or validated by this result.
NimBLE's current persistence/capacity defaults must be addressed for four real TRVs.
The next steps are recorded in the [one-valve bring-up guide](../real-trv-bringup.md).

The read-probe image SHA-256 is
`ecb4101565e4686e1b3ba4bcf4afd73b80c715f9956acf4ccee634fda02811b7`. Its dependency evidence is in the checked-in inventory and local
`build/dependency-review-trv-gatt-read-20261124/`.
