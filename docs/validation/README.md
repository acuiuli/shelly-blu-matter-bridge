# Validation gates and roadmap

Evidence advances gates; documentation or a successful build does not establish RF,
controller or real-valve behavior. Do not start the complete BLE client until transport
feasibility has passed. Keep a spare TRV and vendor recovery option available.

| Gate | Work | Required evidence |
| --- | --- | --- |
| 0: Before hardware | English foundation, pinned toolchain, host tests, cross-build, full matrix | Reproducible checks, explicit mapping blockers and dependency inventory |
| 1: Transport | Commission simulator into existing Thread; Wi-Fi disabled; FTD and post-commission BLE | Matter access over Thread, resolved config, observed eligibility/roles, BLE scan/connection; forwarding when elected router |
| 2: Compatibility | Four climates/valves/batteries; grouping/conformance; BT contribution; automation migration; S3 OTA | Descriptor tree, HA entities, requested/reported behavior, 9.3.0 import/install/rollback report |
| 3: One real TRV | Bond/read/write/reconnect, manual changes, direct mode, maintenance | Original gateway off; reboot-retained bond/identity; confirmed position; every required function tested |
| 4: Four TRVs | Concurrent commands, absent valve, stale telemetry, interference, resets and interrupted updates | Independent recovery and no obsolete replay; memory/latency/error measurements |
| 5: Pilot | Four-valve installation for at least 14 consecutive days, then second gateway/seven valves | No manual recovery, recorded metrics and two unique bridges on the same Thread network |
| Full parity | All required matrix rows including TRV firmware updates | Legitimate standard-only mappings and passing acceptance evidence; otherwise stay experimental |

Every hardware run records command count, confirmed count, failure categories,
latency p50/p95/max, memory free/minimum, reconnect time, stale duration, S3/H2 reset
reasons and manual interventions. Set numeric service objectives after initial real
measurements; do not invent a single timeout for all BLE operations.

Test absent/out-of-range valve, batteries removed/replaced, stale/invalid packets,
manual ring changes during delayed commands, rapid target replacement, gateway/HA/BR
restart, RCP reset, topology role changes, interrupted S3 and TRV updates, and retained
identities after OTA/fabric changes. When Router, demonstrate actual forwarding of
another node's traffic. Child/REED operation must retain Matter and BLE functionality.

Review local TRV behavior without gateway/HA, including external sensor fallback,
frost/window modes and schedules; no safety behavior is inferred from simulator output.
Use the [report template](report-template.md) and attach only sanitized evidence.
