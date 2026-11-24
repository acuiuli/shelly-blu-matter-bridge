# Third-party notices and dependency status

The project source is Apache-2.0. No vendor TRV firmware is incorporated. The
firmware uses the pinned SDKs below. The official Matter image is extended with
ESP-IDF 6.1.1 and the resolved Python/build tools; its digest, recipe and all SDK
revisions are recorded in [toolchain.lock.json](tools/toolchain.lock.json).

| Component | Pinned revision/version | Source | License / required notices |
| --- | --- | --- | --- |
| ESP-IDF | `051e4c4eb20346e043cdd47bc388fde75ff8af63` (6.1.1) | [Espressif](https://github.com/espressif/esp-idf/tree/051e4c4eb20346e043cdd47bc388fde75ff8af63) | Apache-2.0 framework; component licenses and distributed binary-library terms also apply |
| ESP-Matter | `50c810ccee552cb0f3bfac47ec834ab4aa3c79ea` (1.6.1) | [Espressif](https://github.com/espressif/esp-matter/tree/50c810ccee552cb0f3bfac47ec834ab4aa3c79ea) | Apache-2.0; retain license and notices; modification below |
| connectedhomeip | `0998b87db7aeed765852b778e79cb7d2afb1f909` | [Espressif fork](https://github.com/espressif/connectedhomeip/tree/0998b87db7aeed765852b778e79cb7d2afb1f909) | Apache-2.0; third-party component terms also apply |
| OpenThread | `43cc05a9bcf780bd758bad53d897e2d88cf8cb75` | [Espressif fork](https://github.com/espressif/openthread/tree/43cc05a9bcf780bd758bad53d897e2d88cf8cb75) | BSD-3-Clause; retain copyright, conditions and disclaimer |
| Apache NimBLE | `544bb668add673bc8ab66877773cfa86067df1ea` | [Espressif fork](https://github.com/espressif/esp-nimble/tree/544bb668add673bc8ab66877773cfa86067df1ea) | Apache-2.0; retain license/NOTICE and component notices |
| Registry dependencies | Exact versions and content hashes in [dependencies.lock](firmware/dependencies.lock) | [Component Registry](https://components.espressif.com/) | Per-component LICENSE/NOTICE files; included in the collected inventory |
| Compiler/runtime and SDK binary libraries | Container recipe, IDF tool manifests and linked-archive hashes | Official SDK base image and IDF submodules | Includes PHY/Bluetooth archives and compiler runtime terms; review each linked archive before distribution |

ESP-Matter modification: `esp_matter_icd_configuration.cpp` changes its constant
log tag to `const char *` for IDF 6.1.1 compilation. The modified file carries a
prominent change notice. The [patch](.devcontainer/patches/esp-matter-const-log-tag.patch)
is checked by hash and included in the notice inventory.

connectedhomeip modification: `ValveConfigurationAndControlCluster.cpp` rejects
percentages above 100 before accepting Open commands or DefaultOpenLevel writes.
The modified function carries a change notice; its
[patch](.devcontainer/patches/connectedhomeip-valve-level-range.patch) is checked by
hash and included in the inventory. The application does not initialize a cloud
client or HTTP service.

The resolved registry set has five components: cJSON (MIT), ESP Delta OTA,
ESP Encrypted Image, ESP Secure Certificate Manager and mDNS (Apache-2.0 root
licenses). Retain their complete license texts and any nested component notices.
IDF submodules include further dependencies such as lwIP, Mbed TLS and FreeRTOS;
the framework license does not relicense those components. No license text is
replaced by this summary.

After successful S3 and H2 builds, run:

```sh
./tools/dev.sh python3 tools/dependency_inventory.py build/dependency-review
```

The output includes SDK and recursive submodule revisions, registry content hashes,
selected build components, loaded archive hashes, discovered license/NOTICE texts,
the SDK patches, Dockerfile, Python tool lock, link maps and SHA256SUMS.
[The checked-in inventory](docs/dependencies.json) records the current resolution.
Development Python tools are recorded for reproducibility and are not linked into
the firmware. Refresh the inventory and notices on dependency changes.

**Binary redistribution review remains incomplete.** A maintainer must check
transitive headers, linked archives, compiler runtime exceptions and required
attributions before publishing firmware. No distributable binary release is claimed.
Release bundles must include applicable full license texts, required notices,
dependency inventory and checksums.

Better Thermostat, Home Assistant, Matter Server and vendor update images are
interoperability/research targets. Their software/images are not copied into this
repository or relicensed by this project.
