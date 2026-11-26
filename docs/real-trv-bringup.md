# First real TRV: staged bring-up

Start with one identified valve while retaining its vendor firmware and a recovery
path. The firmware provides discovery, a read-only GATT probe, experimental persistent
BLE enrollment and an explicit valve-only configuration command. The first installed
valve completed pairing and protected reads. The regulator write and clean-start
onboarding still need hardware validation. Verified BLE valves now get a dedicated
Matter endpoint reporting physical position. The four simulated TRVs retain their identities.

## Intended onboarding without the Shelly app

Commission the bridge into HA once using Matter. Enroll each BLE TRV directly with
the bridge over private USB, using the TRV's physical pairing mode. No Shelly account,
app or vendor gateway is a prerequisite for that intended flow. HA will receive the
physical valve through the existing Matter bridge. Each BLE valve needs no Matter
pairing code. Physical thermostat and battery reporting are future steps.

Initial setup must offer **valve-only control** explicitly. Selecting it disables the
TRV's automatic temperature regulator over BLE and verifies the setting on the valve.
HA then controls percentage opening. Creating an HA valve entity alone does not
disable Shelly's regulator. Thermostat `SystemMode` has not been established as a
legitimate mapping for this flag; do not manufacture an HA switch or repurpose a
standard attribute. USB setup does not resolve the full-parity Matter mapping gate.

After enrollment, run `diag` over USB. The `Physical TRV` line gives the endpoint,
reachability, confirmed position and sample age. Slot 0 has the default Matter
label **Shelly BLU TRV 1**. Look for its valve in **Settings → Devices & services →
Matter → Devices** and inspect the entity's `current_position` in Developer Tools →
States. Match the new entity to the Matter integration. The existing Shelly
integration may also have an entity for the same radiator. Leave its heating
automations attached to the existing control path during this read-only test.

The physical Matter endpoint rejects valve commands. HA may still show Open, Close
and position controls. Use `trv_reconnect 0` for an immediate encrypted read and
wait for completion before checking `diag`. Otherwise the bridge polls automatically.

## 1. Identify and record the baseline

Record model, firmware, target, reported position, calibration/errors, automatic
control, minimum opening, temperature limits and attached sensors. Store addresses,
keys and household identifiers privately. The existing gateway's read-only
`BluTrv.GetConfig`, `GetRemoteConfig`, `GetRemoteStatus` and `GetRemoteDeviceInfo`
can provide a baseline; this is a development reference, not a runtime dependency
of the new gateway.

The first installed test valve uses vendor firmware 1.5.0. Its original gateway
reports automatic temperature control off. Direct position writes require that
mode according to the [vendor protocol](https://shelly-api-docs.shelly.cloud/docs-ble/Devices/BLU_ZB/trv/).
Position commands must not silently change that setting or other function bits.

## 2. Discover and read without changing the valve

On the bridge's private USB console:

```text
trv_discover 15
trv_discover status
trv_probe YOUR_TRV_MAC
trv_probe status
diag
```

`trv_discover` actively requests scan responses for 1–30 seconds. It recognizes
Shelly company/model data, not names, and lists bounded anonymous candidates.
Candidate identifiers are temporary diagnostics, not permanent enrollment IDs.
It never connects or initiates pairing. Unknown manufacturer block formats are
rejected rather than guessed; flags absent from a frame remain unknown.

`trv_probe` requires a colon-separated address matching exactly one TRV observed
within 120 seconds. The address argument and console history/capture are private;
status output does not print addresses, passkeys or payloads. The probe uses the
existing NimBLE host and one connection, with a 10-second connection attempt and
a 20-second operation deadline that requests disconnection. It refuses to overlap
scanning, advertising or another BLE connection.

The probe reads only target, position, calibration, enabled-function bits and errors.
It has no characteristic-write or enrollment operation. On the NimBLE host task,
it temporarily clears the bonding-request bit and restores Matter's prior setting
when the connection ends. It does not initialize another host or replace Matter's
callbacks. A passkey request aborts the probe. Authentication failures remain visible;
they are never converted into zero-valued telemetry.

Reads normalize target temperature to centidegrees Celsius and validate lengths
and ranges. Function/error bitmasks remain raw diagnostics, not invented Matter faults.
Read success is not proof that a prior movement command completed.

## 3. Enroll and verify persistent recovery

For an installed valve, retain the original peer and verify capacity first: Shelly
documents four paired peers and eviction of the
least-used peer when adding another beyond that limit. See
[pairing behavior](https://shelly-api-docs.shelly.cloud/docs-ble/common/#pairing).
Do not reset an installed valve merely to fix a pairing failure.

The TRV-specific vendor instructions describe a **single short press** of the reset
button beside the battery cover for BLE pairing. A hold longer than 30 seconds is
a factory reset, not the normal enrollment action. Follow the exact model's
[instructions](https://shelly-api-docs.shelly.cloud/docs-ble/Devices/BLU_ZB/trv/).
After physical pairing mode is active:

```text
trv_discover 10
trv_discover status
trv_enroll 0 SESSION:INDEX
trv_enroll status
trv_reconnect 0
trv_reconnect status
```

Choose the anonymous `SESSION:INDEX` token printed by the latest discovery for
the physically selected TRV in pairing mode. No MAC lookup through a Shelly gateway
is required. A known private MAC is also accepted for migration diagnostics.
Wait for discovery to finish and each operation to reach `state=done`. Successful
enrollment requires `result=0`, encrypted/bonded access and all five reads passing,
including errors. It reserves a private slot identity before pairing, persists BLE
keys using the SDK's existing NimBLE NVS store, and marks the slot verified only
after those reads. Eight local bond slots accommodate four TRVs and headroom.
Full local stores fail rather than deliberately evicting another peer. This cannot
protect an already-full peer list on the TRV; verify its capacity separately.

Pending reservations survive failure/reboot. A pending slot with stored keys can be
recovered with `trv_reconnect`; otherwise retry `trv_enroll` for the same peer after
reopening its pairing mode. Another peer cannot replace it. Removal/replacement
lifecycle commands are not implemented. Corrupt/unknown storage schemas fail visibly;
no keys, fabrics or identity partitions are automatically erased.

The first implementation accepts public/static addresses and unencrypted devices
in pairing mode using BLE Just Works. This produces an encrypted bond, **not MITM
authentication**. PIN/passkey-protected enrollment and rotating-address enrollment
are not implemented; passkey requests abort. Do not weaken an existing device's
security to make enrollment pass. Reconnect/configuration require an existing LTK
and use the controller's stable public identity. A failed reconnect preserves the
slot. Automatic removal of keys on central pairing failure is disabled in the corrected build.

To refresh a stale bond, first verify the valve's peer capacity and put that same
valve into physical pairing mode. Run `trv_discover 5`, wait for completion, select its
current token from `trv_discover status`, then run `trv_repair SLOT SESSION:INDEX`.
This operation verifies the selected address against the stored slot, commits a
pending reservation, drops only that peer's local BLE keys and re-enrolls it using
the same enrollment identity. `trv_repair status` shows completion and read results.
A mismatched peer is rejected. An interrupted repair can be retried on the same slot.

Verify encrypted reconnect after S3 restart and TRV battery replacement before
depending on the bridge. Keep all console captures and identifiers private. Production
at-rest protection remains part of the [security work](../SECURITY.md).

## 4. Configure valve-only control explicitly

After enrollment and encrypted bonded access have been validated:

```text
trv_valve_only 0
trv_valve_only status
```

This command reads the current enabled-functions bitmask, clears only bit 0
(`enable`, automatic temperature control), writes that characteristic if needed,
and reads it back. It succeeds only when the complete mask matches the intended
value, preserving all other known/unknown bits. If already off, it makes no write.
It does not send a position, setpoint or calibration command. BLE write acceptance
alone is not confirmation that configuration persisted or a valve moved.

Repeat the read after S3 restart and TRV battery replacement. Later position control
must refuse writes while automatic temperature control is enabled or its state is
unknown; it must never change this setting implicitly. A TRV reset or another writer
can change it. Coordinate existing writers during configuration/control validation.

## 5. Test a reset valve without app registration

This is a required acceptance test, separate from migrating an already-paired valve.
Removing an HA device or a Shelly app entry is not proof that BLE bonds were cleared.
The TRV's RPC `Shelly.FactoryReset` is documented to preserve pairing information;
do not substitute that for a verified physical clean start.

Keep the current installed-valve baseline and cover its heating/boiler automations
before resetting. Prepare and verify bridge enrollment first. Then remove only the
selected old integration/device, leave this Matter bridge commissioned, and perform
the model's documented physical reset. Record what was actually cleared, default
control mode, calibration and settings. Re-enroll directly without registering in
Shelly, explicitly select valve-only control, and prove encrypted reconnect/configuration
retention after power cycles. A reset, previously registered valve validates that
scenario; a never-registered factory-new valve remains a separate acceptance case.

For the complete test, bind real telemetry and control, verify HA sees the actual
valve through the already-commissioned bridge, and confirm bounded movement by
fresh readback. Verify operation with no vendor gateway assisting the test valve;
do not disconnect a gateway still needed for the other installed valves. The current
enrollment commands do not satisfy these Matter/control acceptance steps.

## 6. Bind one slot, then prove control

Bind only the selected valve to a reserved slot, preserving Matter identity and
the other slots. Replace simulated observations only when real fields are known;
do not fabricate a complete snapshot from partial advertisements. Freshness and
ordering are per field. Device communications must schedule state changes onto
the Matter owner task and discard obsolete command tokens before transmission.
Verify the real device's temperature range against the Matter thermostat attributes
before binding; this test valve legitimately reports a 4°C target. Control-mode
semantics also need validation before enabling mode writes.

Coordinate the test valve's existing HA/gateway writers before movement tests.
Test bounded opening/closure with readback, a replaced command, manual changes,
an absent valve, reconnect and gateway restart. An ATT write acknowledgment is
transport acceptance; confirmed position must come from a fresh valve observation.
Retain temperature locking and the boiler lease. Arrange an independence test with
the original gateway disconnected after the remaining valves' control is covered.

Record latency, success/failure counts, memory and recovery. Then scale to four
valves and the pilot. Calibration, external input and vendor firmware updates remain
separate standard-Matter mapping/implementation gates in the
[capability matrix](matter-mapping.md); basic position control does not establish parity.
