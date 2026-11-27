# Heating setpoint on an enrolled physical TRV

The installed Shelly BLU TRV on firmware 1.5.0 was already enrolled in slot 0
on Matter endpoint 11. Before the S3 update, a 2.282-second encrypted reconnect
read a 20.0°C target, 11% opening, completed calibration, enabled-functions mask
160 (regulator off) and zero errors.

The local S3 build was 1,615,408 bytes with SHA-256
`664646ae7be474d53e9ebbcdd117719ad1be7ad13af2a74320e0e46fe785265e`.
The application-only USB flash verified successfully. After boot the bridge
reattached to Thread, recovered the existing endpoint and bond, and reported
a 20.0°C heating target, 19.9°C measured, 38% battery, a 4.0–30.0°C range
and 11% valve opening. Home Assistant was already commissioned.

## Home Assistant setpoint write and restoration

The operator sent `climate.set_temperature` to the enrolled radiator twice,
first to change its target and then to restore it. The bridge recorded an
acknowledged BLE write for each request, read the target on the same
connection and read it again on a separate encrypted, bonded connection.

| HA target change | Bonded write and readback | Independent reconnect | Opening on both reads |
| --- | --- | --- | --- |
| 20.0°C → 20.5°C | 20.5°C in 8.637 s | 20.5°C in 4.287 s | 11% |
| 20.5°C → 20.0°C | 20.0°C in 7.961 s | 20.0°C in 2.654 s | 11% |

Durations are for the bridge's BLE operations. All four readbacks returned
the original functions mask of 160, completed calibration and zero errors.
The final reconnect confirmed the radiator was back at its original target
with automatic regulation off.

The operator also saw two HA sensors named **Temperatura**, each at 19.9°C.
The endpoint publishes the same measured sample through both Temperature
Measurement and Thermostat. HA creates a sensor for each attribute.
[HA entity mapping](../home-assistant.md#temperature-entities)

## Test scope

The four host test suites and five packaging tests passed. The setpoint
tests cover device limits, unsupported precision, a changed starting target or
regulator mask, expiration, single-use requests and confirmation of both target
and flags. Long-duration control, a radiator regulating its own temperature,
mode changes and device-type conformance have separate acceptance criteria.
