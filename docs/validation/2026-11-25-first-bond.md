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
key-retention checks. The remaining hardware test is to repair the selected peer
and read its protected error register again after rebooting the S3.
