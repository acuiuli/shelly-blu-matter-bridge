# Validation results

The current SDK upgrade and hardware session are recorded in
[2026-11-24 board bring-up](2026-11-24-board-bringup.md). The original foundation
measurements below are historical and use the earlier toolchain.

The [first real-TRV report](2026-11-24-real-trv-read.md) records discovery and a
direct read of one installed 1.5.0 valve while the bridge remained a Thread router.

The [2026-11-25 enrollment preparation](2026-11-25-enrollment-preparation.md)
adds persistent peer reservations, SDK bond storage and an explicit USB valve-only
configuration command. Host checks and non-mutating board guards passed.
The [first installed-valve enrollment and reboot test](2026-11-25-first-bond.md)
records the original reconnect failure and a passing retest with the corrected
S3 identity. The bridge recovered its bond after a hardware reset and read all
five fields, including the valve's protected error register.

The [physical position report](2026-11-26-matter-position.md) records Home Assistant discovery of a physical
valve through the existing bridge and operator confirmation of the displayed changes
during a Shelly-commanded movement test. The report includes the initially conflicting
closure readings, the matching 18% HA action and the independent checks on the
restored position. It also covers the USB guards and the first bonded write from the S3.
The valve reported 20% immediately and 0% on the delayed read.

## Original foundation validation

Recorded **2026-11-10** from the foundation working tree (no release tag). These are
software/build results only. No board, Thread network, Home Assistant instance or real
TRV was exercised in this run.

| Check | Observed result |
| --- | --- |
| Pinned official container and five SDK revision checks | Pass; see [toolchain lock](../../tools/toolchain.lock.json) |
| C++ host build with warnings treated as errors for core/tests | Pass, GCC 13.2.0 / C++17 |
| Reconciliation CTest | 1/1 executable passed; input rejection, requested vs reported state, delayed/superseded/manual commands, expiry, bounded retries, stale telemetry, four-valve isolation, reconnect and stable identity checks |
| Release packaging tests | 4/4 passed: unreviewed/stale input and changed-license rejection, and license/checksum bundle integrity |
| DCO checker fixtures | Matching author sign-off accepted; unsigned commit rejected |
| ESP32-S3 firmware | Cross-compilation passed, Xtensa GCC 14.2.0; 1,560,048 bytes (0x17cdf0) |
| S3 OTA partition fit | Two 0x3E0000 slots; about 62% of each slot remains free for this image |
| ESP32-H2 RCP | Cross-compilation passed, 208,944 bytes; UART0 RX23/TX24, example baud 460800 |
| Resolved Kconfig | FTD, UART RCP, Matter Thread, central/observer BLE and rollback/OTA enabled; Matter Wi-Fi roles and commissioning-only BLE disabled |
| clang-format 18.1.3 | Pass for project C++ sources |
| Offline repository/doc checks | Pass after result/inventory generation |
| External documentation links | 17 unique public URLs checked without a broken-link result; repository-private settings links excluded |
| Dependency inventory | 20 registry components, 190 linked archives across both targets, 438 license/notice files collected; all archive hashes resolved |
| Private draft/history | Original retained under ignored `.private/`; existing tracked history contains only LICENSE |

S3 application SHA-256: `ab591b188a04d26cdeb51ac23856537a0625b15c91f492e2982866135ff8d831`

H2 RCP application SHA-256: `c3a62e05c9a81c21bc90d3bcec383b0441adfcb1a1edaa4c7af70fd6e3081a7e`

These identify local application binaries, not reviewed release bundles or Matter OTA
wrappers. Full source/notice provenance is in [dependencies.json](../dependencies.json)
and locally generated `build/dependency-review-final/`. Normal CI regenerates its own
review directory and retains the notice evidence artifact. The registry lock is included
as source configuration; SDK revisions are fixed by the container digest.

## Pending evidence and release gates

The [current board report](2026-11-24-board-bringup.md) establishes Thread attachment
and router election, Wi-Fi uninitialized, runtime heap and passive BLE reception
after successful HA commissioning. The user confirmed four simulated climates,
four valve controls and battery entities. Test-DCL trust is required on the controller
for this development image. Mesh forwarding performance, Wi-Fi RF inactivity,
device-type conformance and a full review of HA composition/grouping
remain **not run**. Real-device command latency, RF
coexistence under Thread traffic, BLE motor control, persistence through OTA,
9.3.0 OTA import/install/rollback, all maintenance functions, TRV updates and the 14-day
pilot remain **not run**.
The [capability matrix](../matter-mapping.md) identifies unresolved standard mappings.

Binary-library redistribution review remains incomplete. Current SDK source patches
are recorded in the dependency notices; no TRV firmware images were introduced.
SDK warnings about duplicate Kconfig definitions
and unused/uninitialized values in unused vendor clusters were observed; they did not
fail the builds. They are upstream warnings, not hardware qualification results.

The foundation is pushed to GitHub and the repository is **private**. Public launch
requires a working MVP; this software-only foundation does not meet that gate. Workflow
defaults are read-only and workflows cannot approve pull requests. Hosted verification
is tracked by the [CI workflow](https://github.com/acuiuli/shelly-blu-matter-bridge/actions/workflows/ci.yml).
Consult the run for the revision being used; the local measurements above are separate.

Branch protection is unavailable on the current private-repository tier (GitHub returned
403), and private vulnerability reporting is unavailable for this private repository
(404). Configure and verify those controls at the eventual public launch. No paid plan
change is required or authorized by this foundation.
