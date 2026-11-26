# BLE protocol investigation

The [vendor BLU TRV documentation](https://shelly-api-docs.shelly.cloud/docs-ble/Devices/BLU_ZB/trv/)
describes periodic BTHome observations and bonded GATT access. The bridge polls the bonded peer's position characteristic and publishes validated
percentages on its physical Matter endpoint. Bounded discovery, a non-bonding read-only GATT probe,
experimental persistent enrollment and explicit valve-only configuration are
available; see [one-valve bring-up](real-trv-bringup.md) and the
[first direct read](validation/2026-11-24-real-trv-read.md). Do not confuse older Wi-Fi Shelly TRVs with the BLU model, or
`BluTrv.Call` on a Shelly gateway with a direct valve RPC.

| Operation | Characteristic UUID | Investigate |
| --- | --- | --- |
| Target | `4936d0ba-ff05-472c-907b-6551898fc016` | Signedness, ×10 °C to ×100 Matter conversion, limits and readback |
| Position | `c3020a1d-f05b-4430-b2e9-f97df761c33b` | Percentage; writes require automatic temperature control disabled |
| Calibration | `6db6f15e-43d6-425e-8669-5b449ab08250` | Start and completion/error semantics |
| External temperature | `5cb70992-381e-4c21-84d8-b7fd1173597e` | Units, validity lifetime and fallback |
| Enabled functions | `7ff16da2-6f68-4dc6-a97a-bee89d36e739` | `trv_valve_only` explicitly clears bit 0, preserving unrelated flags and verifying full readback; hardware validation pending |
| Errors | `00a347f9-4a0a-4ad4-a2d5-1cc01f9b3fe8` | Decode only documented flags |

USB enrollment uses the SDK's existing NVS bond store and a separate versioned
physical-peer reservation. Initial Just Works enrollment does not claim MITM
authentication. PIN-protected devices are rejected. Mode configuration requires a
verified binding and encrypted bonded access; it never changes mode as an implicit
side effect of a percentage command. No position/setpoint writes exist yet.

First prove one valve can bond, read, write and reconnect with the original gateway
powered off. Store bonding material in secure device storage and keep it out of ordinary
logs. Verify manual ring changes, packet ordering/wraparound, encrypted advertisements,
GATT timeouts, authentication failures and reconnection after batteries are replaced.

A transport adapter must validate complete frames, units and ranges before calling
`Engine::observe`. RPC success records transport acceptance; readback confirms state.
Use per-field observation ordering when implementing partial BLE telemetry: the current
simulator supplies complete snapshots. Manual-change detection and device reboot
sequence handling need real protocol evidence. Maintain a bounded transaction per valve,
check command tokens immediately before writes, and never change direct-control flags
implicitly in response to a position command. Configure the intended mode explicitly
through validated enrollment/configuration after its standard mapping is resolved.

Only after the Thread/BLE transport proof should this adapter replace the simulator.
Version discovery and firmware-transfer research are separately tracked in [updates](updates.md).
