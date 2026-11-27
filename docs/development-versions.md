# Development versions and workflow names

Version audit: **2026-11-24**. Use the latest stable upstream releases, then retain
immutable Action commits, container digests, SDK git revisions and resolved package
versions. A moving `latest` tag is not a dependency lock.

| Managed component | Current version | Pin / source |
| --- | --- | --- |
| GitHub checkout Action | 7.0.1; Node 24 runtime | `3d3c42e5aac5ba805825da76410c181273ba90b1`; [upstream release](https://github.com/actions/checkout/releases/tag/v7.0.1) |
| GitHub artifact upload Action | 7.0.2; Node 24 runtime | `cf430e030ddbb5b0abf93d22962f4752f3646cd9`; [upstream release](https://github.com/actions/upload-artifact/releases/tag/v7.0.2) |
| GitHub hosted Linux runner | Ubuntu 26.04 | Explicit current stable label; [runner image inventory](https://github.com/actions/runner-images) |
| ESP-Matter | 1.6.1 | Official released image digest and SDK revision in [toolchain.lock.json](../tools/toolchain.lock.json) |
| ESP-IDF | 6.1.1 | Exact [release source](https://github.com/espressif/esp-idf/releases/tag/v6.1.1) revision, installed over the official Matter image's IDF 6.0.2 base |
| OpenThread / NimBLE / connectedhomeip | SDK-matched revisions | Exact gitlinks in the toolchain lock and dependency inventory |
| CMake | 4.4.5 | Exact Python package version in the development image |
| Ninja | 1.13.2 | Exact Python package version in the development image |
| clang-format | 23.1.5 | Exact Python package version in the development image |
| esptool | 5.7.0 | Resolved IDF Python environment; also used for the Windows hardware session |
| Python tools | Fully resolved set | [.devcontainer/python-tools.lock](../.devcontainer/python-tools.lock) |

The Node runtime is declared by each upstream Action; both current Actions use Node 24.
Transitive firmware libraries follow the current stable SDK's version constraints and
submodule revisions. Compatibility baselines such as Matter Server 9.3.0 and Better
Thermostat 2.0.0-Beta2 are recorded test targets, not dependencies installed by CI.
The installation upgraded Better Thermostat to official 2.0.0. Its [client requirements](home-assistant.md) are tracked separately. Record the actual controller versions in each hardware report.

## What the GitHub workflows do

- **Check repository, formatting and host tests** runs on pushes to `main`, on pull
  requests and on manual dispatch. One Linux job checks documentation, release-tool
  tests, PR sign-offs, C++ formatting and host tests. It installs the three pinned
  host tools directly on the runner and has a five-minute timeout.
- **Manually build ESP32-S3 and H2 firmware and audit dependencies** runs only on
  manual dispatch. Use it when a hardware milestone, toolchain change or release
  needs a clean cross-build. In **Actions**, select the workflow, choose **Run
  workflow** and select the intended ref. The CLI equivalent for `main` is
  `gh workflow run firmware.yml --ref main`. It checks SDK revisions, compiles
  both chips, checks the radio/OTA configuration and dependency lock, then uploads
  the dependency review.
- **Check documentation URLs for broken links** runs weekly and on manual request.
  It checks public Markdown links for HTTP 404/410 responses. The repository check
  validates local links on pushes to `main` and on pull requests.

## Reproducible container recipe

The official Matter release image is fixed by digest. The checked-in Dockerfile
advances IDF to the exact 6.1.1 commit and installs the resolved Python tool set.
`tools/dev.sh` builds a locally tagged image whose name includes a hash of the build
inputs. VS Code uses the same Dockerfile, base image and IDF revision. Only the small
`.devcontainer` context is sent to Docker; private hardware captures are outside it.

ESP-Matter 1.6.1 needs one const-correctness compatibility patch under IDF 6.1.1:
`esp_matter_icd_configuration.cpp` declares its constant log tag as `const char *`.
The patch carries a modification notice, is checked by hash, and is included in the
notice inventory. No SDK warnings are globally suppressed to accommodate this change.

A second pinned patch adds the missing 100-percent upper bound to connectedhomeip's
native Valve Configuration and Control validation. It covers Open target levels and
DefaultOpenLevel writes; minimum and level-step rules remain in the upstream handler.
Both patches are included in the dependency inventory with modification notices.
