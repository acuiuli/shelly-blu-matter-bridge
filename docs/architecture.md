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
| Device communication | `bridge::Simulator` and single-connection bonded GATT reader | Deliver validated observations from the selected peer to the Matter task |
| Reconciliation | `bridge::Engine` | Commands carry monotonic expiry and generation token; telemetry is authoritative |
| Matter representation | `firmware/main/app_main.cpp` | Standard clusters; callbacks enqueue intent, periodic owner task publishes observations |
| Persistence | Separate `bridge_store` NVS identity/binding partition; SDK NimBLE NVS keys | Versioned physical-peer reservations; no command persistence |
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
flash erasure destroys identity. The experimental USB enrollment table reserves
physical peer/slot bindings and independent random enrollment identities, rejects
duplicates and never replaces a nonempty slot with a different peer. Its schema and
lengths are explicitly serialized; BLE keys remain in the SDK's NVS store. A pending
reservation survives interrupted pairing. Promotion requires encrypted bonded access
and validated reads. A USB forget operation persists a removing state before
deleting one peer's local bond and Matter endpoint. A removing slot rejects
control and enrollment until cleanup completes. Re-enrolling in an empty slot
creates a fresh enrollment identity and Matter endpoint.

A verified physical valve gets a separate Bridged Node and Water Valve on one endpoint
under Aggregator 1. The SDK assigns its number after startup. The bridge stores that
number with the enrollment identity and resumes it on subsequent boots. Its UniqueID
derives from that identity. The four simulated TRVs keep their own endpoints.

The NimBLE task posts a dated position sample after verifying the bonded peer and
decoding its percentage. The Matter task updates CurrentLevel, CurrentState and
Reachable. Polling targets five minutes. A sample expires at ten minutes and thirty
seconds. At boot a new observation is required. A failed read cannot refresh the age.
Each physical endpoint registers a Valve Configuration and Control server for its
enrollment. Open and Close reserve a single bonded BLE operation and publish the
requested target while it runs. Repeated or concurrent requests receive Busy when
the radio is occupied. The target expires when the operation finishes. Confirmed
levels come from the encrypted TRV reads. Unavailable samples produce a null level.
The preflight requires a matching enrollment, a disabled regulator, successful
calibration and clear errors. Each motor write also requires the expected starting
position. When the bonded read already matches an absolute Matter target, the
bridge verifies it twice without actuating, even if the last published level differed. A command has a 15-second submission
deadline and is never replayed. Finite-duration commands remain a conformance gap.
Clean-start onboarding and closure under the installation's heating controls require
separate validation.

Bonded outbound GATT connections use the S3 controller's public Bluetooth identity.
The pinned Matter SDK configures a new random static commissioning address at boot.
NimBLE's automatic address selection prefers that random address, so it is unsuitable
as the long-lived identity presented to a paired TRV. Address selection for outbound
valve connections is local to each connection. A missing public identity stops the
operation before the bridge attempts pairing.
