# Architecture and decisions

## Scope and staged decisions

The S3 owns the Matter server, OpenThread host, NimBLE, device state and persistence.
The H2 runs only an ESP-IDF OpenThread RCP, connected over the board UART (S3 RX 17,
TX 18, UART1 at 460800 baud). The existing border router provides LAN access. The
TRVs remain BLE devices running original firmware. No autonomous boiler logic,
Better Thermostat algorithm, Wi-Fi backbone or cloud dependency is introduced.

This is an application Thread FTD, not the SDK Thread border-router example.
OpenThread selects Router/REED roles. Configuration, attachment and actual mesh
forwarding need separate evidence. The SDK initializes the FTD device type through
ESP-Matter; the build also checks the resolved Kconfig values.

Thread service discovery uses the SDK's platform DNS-SD backend with OpenThread
SRP and DNS clients. `CONFIG_USE_MINIMAL_MDNS=n` is required: the minimal mDNS
backend cannot replace SRP registration through the existing border router. CI
checks both the resolved Kconfig and generated `chip_mdns = "platform"` build argument.

SDK transitive dependencies still select the Wi-Fi component at compile time. Both
Matter Wi-Fi roles are disabled, and application startup fails if the Wi-Fi driver is
initialized. USB `diag` also reports this invariant. A compiled SDK component is not
evidence of an active radio; measure the invariant and RF behavior on hardware.

If the S3 Thread host cannot be made reliable, investigate S3 BLE plus H2 Matter/Thread
with an application UART protocol. This contingency is **not implemented**; H2 memory,
endpoint capacity and the protocol would require a separate proof. Do not develop both
architectures concurrently.

## Boundaries

| Layer | Current implementation | Extension contract |
| --- | --- | --- |
| Device communication | `bridge::Simulator` | Replace with asynchronous BLE transport; one transaction per TRV; validate frames before observations |
| Reconciliation | `bridge::Engine` | Commands carry monotonic expiry and generation token; telemetry is authoritative |
| Matter representation | `firmware/main/app_main.cpp` | Standard clusters; callbacks enqueue intent, periodic owner task publishes observations |
| Persistence | Separate `bridge_store` NVS partition | Schema/version and gateway identity; no command persistence |
| Diagnostics | USB `diag`, simulator injection | No dataset, PIN, bonding keys or credential logs |

The core has no SDK dependency, radio assumptions or heating controller. This small
transport boundary is enough for future devices; no plugin framework is introduced.
Bridged-device state mutations run on the Matter task. USB simulator work is scheduled
onto it. BLE diagnostics keep separate bounded caches on the NimBLE task with locked
snapshots for USB access. A real BLE transport must serialize observations onto the
Matter task and discard
outdated tokens before writes; a write already transmitted cannot be undone. In that
case reread actual state and converge to the latest intent without replaying the queue.

## State semantics

Accepted, sent and physically confirmed are separate. Targets are desired state;
`CurrentLevel` only follows validated device telemetry. Acknowledging an RPC is not
motion confirmation. New commands replace older intent for the same field. Retries
are bounded to three; commands expire after 15 seconds; telemetry becomes stale after
30 seconds. These are simulator defaults to revise using real BLE measurements.
Manual observations cancel pending intent for that field. Invalid or older telemetry
is rejected. Other valves keep progressing when one is absent. No pending command is
restored after reboot. Off never manufactures a closed-valve report.

## Provisional endpoint layout

Root endpoint 0 includes gateway identity and OTA Requestor; Aggregator is 1. Four
stable bridged thermostat parents use 2, 4, 6, 8; child Water Valve endpoints use
3, 5, 7, 9. Each parent has Bridged Device Basic Information and Power Source.
Descriptor parent links express composition. Water Valve device type is used because
HA valve discovery requires it. Whether a radiator actuator legitimately meets that
device type and whether HA groups its child with the thermostat are **open conformance
gates**. This layout is an experiment, not a frozen public ABI or certification claim.
Do not map valve percentage to heating demand, On/Off, fan speed or window covering.

A random 128-bit gateway identity is stored once in a dedicated NVS partition. Stable
slot ids derive from it, so gateways differ. Creation order is checked against reserved
endpoint ids. Matter fabric changes and S3 OTA do not intentionally erase that partition.
Hardware tests must prove this, including default SDK factory reset behavior. Factory
flash erasure destroys identity. A future USB enrollment table must persist physical
TRV-to-slot bindings, reject duplicates and never recycle a removed slot silently.
The prototype reserves all four slots for simulators; it does not enroll real valves.
