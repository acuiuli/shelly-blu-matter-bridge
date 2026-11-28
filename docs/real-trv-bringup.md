# First real TRV: staged bring-up

Start with one identified valve while retaining its vendor firmware and a recovery
path. The firmware provides discovery, a read-only GATT probe, experimental persistent
BLE enrollment and an explicit valve-only configuration command. The first installed
valve completed direct enrollment after a physical reset. With all slots empty,
the bridge created a fresh bond and disabled the valve's automatic regulator.
[Factory-reset enrollment measurements](validation/2026-11-27-direct-enrollment.md#first-enrollment-after-a-physical-factory-reset).
Verified BLE valves get a dedicated
Matter endpoint reporting physical position. The four simulated TRVs retain their identities.

## Intended onboarding without the Shelly app

Commission the bridge into HA once using Matter. Enroll each BLE TRV directly with
the bridge over private USB, using the TRV's physical pairing mode. No Shelly account,
app or vendor gateway is a prerequisite for that intended flow. HA will receive the
physical valve through the existing Matter bridge. Each BLE valve needs no Matter
pairing code. The same physical endpoint exposes measured temperature and battery
as read-only Matter sensors. Its Thermostat cluster also accepts a heating setpoint.

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

The physical Matter endpoint accepts standard Open, Close and percentage commands. Use `trv_reconnect 0` for an immediate encrypted read and
wait for completion before checking `diag`. Otherwise the bridge polls automatically.
On the console, `Physical sensors` reports published temperature in hundredths
of a degree Celsius and battery in whole percent. `trv_reconnect status` reports
the optional sensor read separately from the position read. A missing or
unsupported sample can leave the valve available while its sensors have no
current value.

If an existing HA valve shows only battery after upgrading the S3, check
`diag` for a published temperature first. On the parent Matter bridge's
device page, open **Device info → ⋮ → Re-interview device**. Then check the
physical valve's **Sensors** section again. HA discovers temperature from
Temperature Measurement's MeasuredValue. [HA sensor discovery](https://github.com/home-assistant/core/blob/dev/homeassistant/components/matter/sensor.py) · [HA Matter device interview](https://www.home-assistant.io/blog/2024/02/07/release-20242/#matter-diagnostics-and-actions)

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
reopening its pairing mode. Another peer cannot replace it. Corrupt/unknown storage schemas fail visibly;
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

On a failed repair, check the slot and `trv_reconnect SLOT` first. Repair removes
the old local key before the new pairing completes, so the Matter valve can
become unreachable. If the slot is pending and reconnect reports
`BLE_HS_EAUTHEN`, start a short discovery scan as the operator opens a fresh
pairing window. Use `trv_enroll SLOT SESSION:INDEX` for the same peer and pending
slot. Verify all five reads over the bonded link and a second encrypted
connection before using the valve in HA. One installed valve recovered its
original endpoint using this procedure after the Shelly gateway association
was removed. The TRV displays `bLE` during its 30-second window.
[Recovery measurements](validation/2026-11-27-direct-enrollment.md#renewing-the-bond-after-gateway-removal) · [TRV button and display](https://kb.shelly.cloud/knowledge-base/shelly-blu-trv)

Verify encrypted reconnect after S3 restart and TRV battery replacement before
depending on the bridge. Keep all console captures and identifiers private. Production
at-rest protection remains part of the [security work](../SECURITY.md).

### Forget a valve on the bridge

Use `diag` to identify the physical slot and endpoint. Over private USB, run
`trv_forget SLOT`. If the valve's Bluetooth address is known, pass it as an
additional argument to require an exact match. The command works with the
valve offline. It removes the selected local BLE bond, the bridged Matter
endpoint and its persistent enrollment. The valve's own settings and peer list
are stored on the radiator. The bridge stays commissioned in Home Assistant.

Check for `result=ESP_OK`, `state=empty` in `trv_enroll status`, and the
endpoint's absence from `diag`. A busy reply means a BLE operation is in
progress. If cleanup is interrupted, the slot shows `removing`; repeat the
same forget command. New enrollment in that slot receives a new Matter
identity. Check the resulting HA entity before restoring its automations.

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
before resetting. For an empty-slot test, also forget the selected valve on the
bridge and verify the slot is empty. Leave the Matter bridge commissioned.
Perform the model's physical factory reset once. A valve already reset with no
paired peers is ready for the same test. Record what was actually cleared, default
control mode, calibration and settings. Re-enroll directly without registering in
Shelly, explicitly select valve-only control, and prove encrypted reconnect/configuration
retention after power cycles. A reset, previously registered valve validates that
scenario; a never-registered factory-new valve remains a separate acceptance case.

For the complete test, bind real telemetry and control, verify HA sees the actual
valve through the already-commissioned bridge, and confirm bounded movement by
fresh readback. Verify operation with no vendor gateway assisting the test valve;
do not disconnect a gateway still needed for the other installed valves.

### Isolate an installed valve while the other radiators stay on Shelly

Save the selected valve's gateway component ID, peer identity, local and remote
configuration, schedules and latest status privately. Find the component in
`Shelly.GetComponents` and verify its address with `BluTrv.GetConfig`. The
gateway's aggregate `Shelly.GetStatus` can omit dynamic TRV components.

During the agreed test window, remove only the matched component with
`BluTrv.Delete`. Read the component directory again and compare the remaining
TRV configurations with the snapshot. An old HA entity may remain visible after
its backing component is removed. Keep the commissioned Matter bridge in HA.

For a valve already verified in a bridge slot, run `trv_reconnect SLOT` after
removing the gateway association. If the encrypted reads succeed, ordinary
migration is complete for the BLE link. To test fresh keys deliberately, open
the physical pairing window and follow the same-slot repair procedure above.
Plan for the control outage if the valve declines. A valve new to the bridge
uses `trv_enroll` in an empty slot. Record which path was exercised.
[Installed-valve isolation result](validation/2026-11-27-direct-enrollment.md)

## 6. Compare an externally commanded movement with the Matter report

Use an enrolled valve in valve-only mode with a known starting position. Coordinate
with whoever controls the installed radiator so a second writer cannot change its
position during the test. Keep the existing boiler lease and temperature lock in
service. If the valve is under active control, observe a normal heating cycle.

1. Read the position, setpoint, regulator flag and errors through the existing
   Shelly control. On the bridge, run `trv_reconnect SLOT`, wait for
   `trv_reconnect status` to show a successful encrypted read, then check `diag`.
   Record the physical endpoint, position and sample age. In HA, open the Matter
   valve's `current_position` in Developer Tools → States.
2. Use the original Shelly control to request the agreed small opening, then
   closure. After each movement trigger a fresh encrypted read. Record the valve's
   own percentage, the newly published percentage on the same endpoint and any
   value observed in HA.
3. Restore the position measured immediately before the test. If the valve started
   open, this requires a final command and fresh readback after testing closure.
   Confirm restoration on both paths and compare the regulator, setpoint and errors
   with the baseline. If another writer takes control, end the test and coordinate
   restoration with the operator.

This procedure uses the Matter endpoint only for observation. Its automatic poll interval is five minutes,
so a brief movement between polls requires an explicit refresh to be seen.

## 7. Test a direct USB position command

Use `trv_position SLOT EXPECTED_PERCENT TARGET_PERCENT` on the private USB
console. Inspect completion with `trv_position status`. For a no-movement check,
pass the observed position for both percentages. A differing live value rejects
the request. An equal live value returns success without sending a motor command.

For a movement test, coordinate the existing controller first. The bridge rejects
a busy connection, an unverified slot, an enabled regulator, a calibration or
device fault, a changed starting position, or a request that reaches the write
step 15 seconds after submission. The preflight and write are separate BLE
operations. An independent controller can still intervene. The USB request runs
once and is never stored or automatically retried.

`submitted` and `acknowledged` record whether the position write was sent and accepted.
`first` is a freshly decoded position and `later` is another read scheduled
12 seconds later on the same encrypted connection. An equal-position request
also performs both readbacks. `trv_position status` shows the active or latest
position request. `trv_position history` shows the two most recent completed
USB or Matter position requests across all slots, newest first. It records the
expected start, target, preflight percentage, both readbacks and the write result.
Polling preserves the history. Restart clears it. Each valid observation is
published to Matter. A final mismatch returns an error while retaining the actual
position. A missing final read marks the prior publication unavailable until a
fresh poll. A value of -1 in the USB readbacks means no reading was obtained for
that phase. The prototype interval was chosen to cover the observed nine-second
reopening. Boiler lease decisions remain in HA.

## 8. Prove direct Matter control

Use the physical valve under the Matter integration. Confirm its endpoint with
`diag` and read its position with `trv_reconnect SLOT` before changing anything.
Coordinate other writers for this radiator and record its live starting position.

1. In Developer Tools → Actions choose **Valve: Close** if the valve is already
   at 0%. Target the Matter valve and run the action. This exercises the command
   path and both bonded reads without requiring a motor write.
2. For a movement test choose **Valve: Set valve position** and enter an agreed
   small opening. Check `trv_position status` over USB for `operation=matter_position`,
   the acknowledgment and both readbacks. Check the Matter entity's
   `current_position` in HA. A delayed mismatch ends the request with an error.
   When the fresh preflight already equals the target, Matter proceeds to both
   readbacks without writing. A third position rejects the request. USB retains
   its explicit expected-start check.
3. Restore the measured starting position using the same Matter entity. Use
   **Valve: Close** for zero. Read `trv_position history` to record both operations. Check the HA state and percentage together.
4. On the same boot, compare `reachability_losses` in `diag` before and after
   the cycle. It counts reports that this endpoint became unreachable. A healthy
   cycle should leave the counter unchanged and HA should keep the valve
   available during both commands. The last observed percentage is valid while
   the next read is in flight. A failed write confirmation still expires it.

The bridge accepts one BLE operation at a time. A busy request must be issued
again by the caller after fresh feedback. Finite timed opens and finite default
durations are unsupported in this prototype. Verify concurrent clients, an absent
valve, restart, and operation with the original gateway absent for this radiator
before using this control in an automation.

## 9. Test a physical thermostat setpoint

1. On the private console run `diag` and record the enrolled endpoint, current
   setpoint, regulator, opening and limits. Temperatures in the diagnostics
   are in hundredths of a degree Celsius. Pick an isolated radiator and a
   small reversible target change. With the regulator enabled, a new setpoint
   can move the valve.
2. After upgrading an existing bridge, re-interview the parent Matter bridge
   in HA and open the enrolled radiator. Under **Developer Tools → Actions**,
   choose **Climate: Set target temperature** (`climate.set_temperature`),
   select its physical climate entity and supply just the new temperature.
   Leave the optional HVAC mode field blank. In valve-only mode HA reports
   that the thermostat is off and the percentage valve remains usable.
3. Run `trv_setpoint` after the operation completes. Check the expected and
   requested values, write acknowledgment, returned target, regulator flags
   and opening. The bridge reads the target independently after writing.
4. Restore the starting temperature using the same HA action. Read the
   resulting target and flags again, then check them on a fresh bonded
   connection with `trv_reconnect SLOT` and `trv_reconnect status`.

## 10. Check availability and recovery after a battery interruption

1. Choose a radiator that can be offline for about 11 minutes. Leave the
   bridge powered and the radiator mounted. Record the physical endpoint,
   opening, target, regulator flags and `reachability_losses` from the last
   successful bonded read and `diag`.
2. Remove one AA battery from that radiator. Between automatic BLE polls,
   read `diag` over USB. The timeout runs from the **last confirmed position**
   and expires at 630,000 ms. A failed poll leaves the sample timestamp alone.
   To measure automatic recovery, let the scheduled polls run throughout.
3. When `reachable=0`, record the age of the last sample and the loss counter.
   The console uses `confirmed_position=-1` for unknown. In HA, check that the
   enrolled valve is unavailable. Check the physical climate and sensors too
   if they are in use. An unavailable valve supplies no closure confirmation.
4. Put the battery back with its original polarity. On the same endpoint,
   watch for a successful automatic, encrypted, bonded position read and fresh
   sensor values. Note the elapsed time and the actual opening. Check whether
   HA recovers without a device re-interview.
5. After automatic recovery, run `trv_reconnect SLOT` once. Compare the
   returned target, full functions mask, position, calibration and errors
   with the baseline. In the firmware 1.5.0 test, the radiator came back at
   100% open with a 4.0°C target. Restore the agreed baseline explicitly
   when needed, then confirm it on the radiator. [Observed recovery](validation/2026-11-28-battery-interruption.md)
