# First real TRV: staged bring-up

Start with one identified valve while retaining its vendor firmware and a recovery
path through the original gateway. The current firmware supports discovery and a
read-only GATT probe. It does not enroll a valve persistently, write its settings
or feed real telemetry into the Matter endpoints. The four HA devices remain simulators.

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

## 3. Implement secure enrollment and persistent recovery

Before enabling writes, implement a selected-peer enrollment flow, the needed
authenticated BLE access and persistent bond storage. Retain the original peer
and verify capacity first: Shelly documents four paired peers and eviction of the
least-used peer when adding another beyond that limit. See
[pairing behavior](https://shelly-api-docs.shelly.cloud/docs-ble/common/#pairing).
Do not factory-reset an installed valve as an enrollment shortcut.

The current prototype has NimBLE NVS bond persistence disabled and a three-bond
local default. Those settings are insufficient for the four-real-TRV target.
Complete storage, capacity and credential-redaction work before motor commands;
production at-rest protection remains part of the [security work](../SECURITY.md).
Verify encrypted reconnect after S3 restart and TRV battery replacement before
depending on the new gateway. Keep keys and passkeys outside source and ordinary logs.

## 4. Bind one slot, then prove control

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
