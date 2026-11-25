# First direct bond with an installed BLU TRV

The operator observed one existing paired device on the selected firmware-1.5.0
valve and opened its Bluetooth pairing window with the physical button. The S3
matched the selected valve, enrolled it in slot 0 in 2,455 ms and reconnected in
1,635 ms without further input. Both sessions reported encrypted, bonded BLE
Just Works connections. All five GATT reads succeeded. The target was 4°C, valve
opening was 0%, calibration was complete, the function mask was 160 (automatic
control disabled), and the error mask was zero. Earlier, reading that error mask
without a bond had returned an ATT authentication error.

On firmware revision `7a07d18`, the S3 restarted and reattached as a Thread router.
The slot still appeared verified. The first reconnect ended before encrypted reads
in 3,785 ms (NimBLE status 7). The next attempt failed in 37 ms (status 23).
The original gateway could still read the selected valve, reporting 0% and 4°C.

The pinned Matter SDK generates a random static commissioning address at startup.
The pinned NimBLE address-selection helper prefers the random identity, and its
default central failure policy can remove keys on an encryption error. The original
trace did not capture a controller-level disconnect reason. The corrected bridge
selects the S3 public identity for bonded outbound connections and disables that
automatic key-removal policy. A physical, same-slot USB repair supports installations
with a stale bond while preserving the physical-device binding.

The corrected S3 image was flashed and verified by flash readback. Its SHA-256 is
`b4d6bbf64da5fee09293ecb2b4459718bd2ec779395ebedc43b111e83a5e33dc`.
The board returned to Thread with its fabrics present. Slot 0 retained its
reservation and the other three slots were empty. Four host test suites passed,
including rejection of a different peer or empty slot during repair and recovery
of the same enrollment identity. The resolved SDK configuration passed the BLE
key-retention checks.

## Reconnect with the corrected firmware

The operator opened the pairing window once on the same installed valve. Using
the image above (source revision `8d6697b`), the bridge matched the selected
peer and completed repair in slot 0. A USB hardware reset then restarted the
S3. The console captured the ROM boot marker and OpenThread returned to router
role with router eligibility enabled. Wi-Fi remained uninitialized.

| Operation on slot 0 | Elapsed | Result |
| --- | ---: | --- |
| Repair with the valve in pairing mode | 2,623 ms | Encrypted, bonded connection and five successful GATT reads |
| Reconnect after S3 reboot | 7,399 ms | Same verified slot, stored bond and five successful reads including the protected error register |
| Run explicit valve-only setup after reconnect | 1,745 ms | Fresh read showed automatic regulation already disabled, so the command skipped the write |

Each transaction reported a 4°C target, 0% opening, completed calibration,
function mask 160 and error mask 0. Slot 0 stayed verified and slots 1–3 stayed
empty. A separate local read through the original gateway took 1,015 ms and
reported the same position and target. These are individual transaction times
on one valve. The reconnect used the stored key with no further pairing gesture.

This run verifies bond retention across one S3 hardware reset. Actual regulator
on-to-off writes, TRV battery replacement, standalone onboarding after a valve
reset, real HA telemetry and motor control still require separate tests.
