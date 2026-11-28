# Availability of an enrolled radiator during a battery interruption

The operator removed one AA battery from an installed Shelly BLU TRV running
firmware 1.5.0. The S3 and H2 stayed powered. The test used the existing HA OS
Matter fabric and the same valve enrolled directly on endpoint 11. The original
Shelly gateway association for this radiator had already been removed. The
bridge ran the [physical thermostat build](2026-11-27-physical-thermostat.md).

Before removal, a scheduled bonded read returned 11% opening, 20.0°C target,
19.4°C measured temperature, 38% battery and functions mask 160 (regulator
off). The bridge was a Thread router and the endpoint had recorded zero
reachability losses on this boot.

The USB observer sampled `diag` and `trv_reconnect status` approximately every
five seconds. These commands inspect state without scheduling a BLE operation.
The sample age is the bridge's elapsed time since a confirmed position read.
The last good position was recorded at approximately 11:54:56 CET. The
operator confirmed the battery was out at 11:55:26 CET. Physical removal and
insertion times are approximate because the operator reports them separately.

## Loss of contact

The first missed automatic poll timed out after about 10 seconds. Subsequent
connection failures left the original sample age increasing. At age 629.103 s,
the bridge still reported 11% open. At age 633.739 s, it reported the endpoint
unreachable and the loss counter had increased from zero to one. That interval
brackets the firmware's 630-second freshness deadline. The position became
unknown. The physical temperature and battery readings cleared, and the bridge
stopped reporting a current thermostat target.

The operator saw both the valve and climate as unavailable in HA and
reinserted the same battery. At 12:07:58 CET the monitor saw endpoint 11
reachable again. The successful scheduled BLE transaction took 1.993 s and
used the existing encrypted bond. The four simulator slots continued to
report reachable, the S3 remained a Thread router, and endpoint 11 recorded
one reachability loss. The minimum sampled free heap was 92,764 bytes, with
a boot minimum of 87,484 bytes.

## What the radiator retained

| Reading | Before removing the battery | First automatic read after reinsertion |
| --- | ---: | ---: |
| Valve opening | 11% | **100%** |
| Heating target | 20.0°C | **4.0°C** |
| Enabled-functions mask | 160 (regulator off) | 160 (regulator off) |
| Measured temperature | 19.4°C | 18.8°C |
| Battery | 38% | 37% |

A separate 2.727-second encrypted reconnect confirmed 100% open, a 4.0°C
target, completed calibration, mask 160 and zero errors. The bridge had sent
only reads during the interruption and automatic recovery. It exposed the
new readings to Matter. On this valve, a retained bond and regulator setting
did not guarantee retention of either the opening or setpoint.

## Restoring the test radiator

After a fresh preflight at 100%, the bridge submitted an explicit
USB request to restore the baseline opening of 11%. The BLE connection ended
before an acknowledgment or either in-transaction readback arrived. A new
4.628-second bonded read found the radiator at 11%, still at a 4.0°C target,
with mask 160 and zero errors. The restore required one submitted position
write. After this separate restore attempt, the endpoint was reachable and
the loss counter read two. The first loss occurred during battery removal.
The later loss is consistent with the bridge invalidating position after the
missing write acknowledgment. A fresh read resolved the uncertain result.

The operator restored the heating target through Home Assistant on the same
physical climate entity. The bridge recorded one acknowledged write from
4.0°C to 20.0°C and read back 20.0°C in the 4.601-second transaction. A
separate 5.127-second bonded reconnect confirmed the original 20.0°C target,
11% opening and functions mask 160. The radiator reported completed calibration
and zero errors.
