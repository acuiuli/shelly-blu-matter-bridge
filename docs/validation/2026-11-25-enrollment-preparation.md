# 2026-11-25: persistent enrollment and explicit valve-only setup preparation

This result covers software checks and non-mutating board guards. It does **not**
establish successful real pairing, bond retention, a regulator configuration write,
or real control through Matter. All four Matter devices remain simulators.

## Implementation

USB `trv_enroll` selects a recently discovered TRV in physical pairing mode, using
an anonymous discovery token or a known private address. It reserves one of four
versioned physical-peer identities before attempting pairing, rejects duplicate
peers/identities and retains pending reservations across failure. A different peer
cannot silently replace an occupied slot. The initial implementation supports
unencrypted devices using Just Works; it does not claim MITM authentication or
support PIN-protected/rotating-address enrollment.

The SDK's existing NimBLE NVS bond store is enabled with eight local bond slots.
Matter retains ownership of host initialization and global callbacks. Enrollment
requires encrypted bonded access and all five allowlisted reads to pass before
marking its reservation verified. `trv_reconnect` requires an existing LTK and
never intentionally deletes or repairs stale bonds. Flash/identity/fabric erasure
is not a recovery action in these commands.

USB `trv_valve_only` is an explicit configuration operation, separate from enrollment
and future position commands. It requires a verified binding and encrypted bond,
reads the enabled-functions mask, clears only automatic-control bit 0, writes only
when needed and verifies the complete mask by readback. No position, temperature,
calibration, PIN or reset write is implemented. A standard Matter mapping for this
regulator flag remains unresolved; USB setup does not satisfy that full-parity gate.

## Software checks

- Four CTest executables passed: reconciliation, Shelly advertisements, scalar
  readback and enrollment storage. Enrollment tests cover schema/length/state
  validation, duplicate rejection, occupied-slot protection, pending/verified
  round trips and rotating-address rejection. All 65,536 possible function masks
  preserve every bit except bit 0 when configuring valve-only control.
- Five release-tool Python tests passed. Formatting, repository/local-link checks,
  resolved transport/bond settings and all locked SDK/tool versions passed.
- ESP32-S3 cross-compilation passed using the existing pinned toolchain. The
  application is 1,597,792 bytes and fits both OTA slots. H2 firmware is unchanged.
- Regenerated evidence contains five registry components, 175 linked archives and
  457 notices. Binary redistribution review remains incomplete.

## Board checks without changing TRV 202

The final S3 image was flashed and read-back verified, preserving application NVS
and `bridge_store`. Two Matter fabrics remained present. Both reconnect and
valve-only configuration of an empty slot returned `BLE_HS_ENOENT` (5). Enrollment
of the selected installed valve outside physical pairing mode was rejected before
reserving a slot. A stale discovery token was also rejected. All four reservations
remained empty; no persistent pairing or characteristic write was requested.

A separate non-bonding read probe of the same installed firmware-1.5.0 valve
completed in **6,136 ms**:

| Field | Result |
| --- | --- |
| Target | 400 centidegrees, 4°C |
| Position | 0% |
| Calibration | 1 |
| Functions | 160 (`0x00a0`), automatic regulator already off |
| Errors | ATT status 261 (`0x0105`), value remains unknown |

The host bonding setting was restored to 1. Thread remained role 3, router
eligibility 1, Wi-Fi uninitialized and zero publication scheduling rejections.
Free heap was 103,416 bytes before and 102,824 after; minimum was 97,232 bytes.
No abort, panic or brownout was observed. Raw USB captures and addresses remain
private. The installed valve's original pairing and settings were left unchanged.

## Remaining acceptance

Real enrollment, protected reads, bond recovery across S3/TRV power cycles and
the regulator write/readback are **not run**. Real telemetry/control binding and
HA discovery after the bridge is commissioned are not implemented. Resetting a
previously registered valve and separately enrolling a never-registered factory-new
one without a Shelly app/account/gateway are required tests. Keep the installed
valve's current setup until the next test stage is ready; see the
[procedure](../real-trv-bringup.md).

Application SHA-256:
`ddbb4e115feff3942c48f2fbadd18e140b79d80e2df52aa8dcb3f548e542dccc`.
Dependency evidence is in [the inventory](../dependencies.json) and local
`build/dependency-review-enrollment-final-20261125/`. The repository was private at this milestone.
