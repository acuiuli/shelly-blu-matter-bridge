# Third-party notices and dependency status

The project source is Apache-2.0. No vendor TRV firmware is incorporated. The actual
firmware build uses the pinned SDKs below; these are incorporated build dependencies,
not a list of merely proposed technologies. No upstream source has been modified.
Application CMake explicitly selects `esp_http_client` to satisfy an omitted requirement
in the SDK's transitive ESP Insights component. Insights/cloud services are not initialized.

| Component | Pinned revision/version | Source | License / required notices |
| --- | --- | --- | --- |
| ESP-IDF | `b774170ff46c393eeb5e495ea37936038d3f4f4f` (5.5.5 image) | [Espressif](https://github.com/espressif/esp-idf/tree/b774170ff46c393eeb5e495ea37936038d3f4f4f) | Apache-2.0 framework; component licenses and binary-library terms also apply |
| ESP-Matter | `256368558724d371737ac0437972207a917f029a` | [Espressif](https://github.com/espressif/esp-matter/tree/256368558724d371737ac0437972207a917f029a) | Apache-2.0; preserve license and notices |
| connectedhomeip | `9c2a63634e880b92e29b25a49543b70a4baf1572` | [Espressif fork](https://github.com/espressif/connectedhomeip/tree/9c2a63634e880b92e29b25a49543b70a4baf1572) | Apache-2.0; third-party subcomponent terms also apply |
| OpenThread | `b678a4f63b6f9397d1a0fa8f31e5b8e0271a4d00` | [OpenThread](https://github.com/espressif/openthread/tree/b678a4f63b6f9397d1a0fa8f31e5b8e0271a4d00) | BSD-3-Clause; preserve copyright, conditions and disclaimer |
| Apache NimBLE | `685675c0128deafdd201c9eb82e61d227364646c` | [Espressif fork](https://github.com/espressif/esp-nimble/tree/685675c0128deafdd201c9eb82e61d227364646c) | Apache-2.0; preserve license/NOTICE and component notices |
| IDF registry dependencies | Exact versions and content hashes in [dependencies.lock](firmware/dependencies.lock) | [Component Registry](https://components.espressif.com/) | Per-component LICENSE/NOTICE files; collect from resolved packages |
| Compiler/runtime and SDK binary libraries | Exact toolchain bytes fixed by container digest; archive hashes collected from link map | Official SDK image and IDF submodules | Includes Espressif PHY/Bluetooth archives and compiler runtime terms; review each linked archive before distribution |

The lock includes transitive registry packages even if dead-code elimination removes
them from the final image. IDF submodules include further dependencies such as lwIP,
Mbed TLS, FreeRTOS and cJSON. Their exact gitlinks and notices are collected recursively;
the framework's Apache license does not relicense those components. No upstream license
text is replaced by this summary.

The resolved registry root licenses identify `espressif/cbor` 0.6.1~4 and
`espressif/jsmn` 1.1.0 as MIT: retain the copyright, permission and warranty text.
The other 18 resolved registry packages have Apache-2.0 root licenses. Nested
subcomponents can have additional terms; their collected notices remain applicable.

After a successful build, run:

```sh
./tools/dev.sh python3 tools/dependency_inventory.py build/dependency-review
```

The output includes SDK and recursive submodule revisions, registry content hashes,
selected build components, loaded archive hashes, all discovered license/NOTICE texts,
the link map and SHA256SUMS. [The checked-in dependency summary](docs/dependencies.json)
records the foundation build's actual resolution. Refresh it on dependency changes.
The collector deliberately flags binary redistribution review as incomplete: a maintainer
must check transitive headers, linked archives, compiler runtime exceptions and all
required attribution before publishing a firmware bundle. **No distributable binary
release is claimed by this foundation.** A release bundle must carry applicable full
license texts, required notices, dependency inventory and checksums.

Better Thermostat, Home Assistant, Matter Server, vendor documentation and vendor
firmware update images are interoperability/research targets, not copied project code.
Their software/images are not included in this repository or licensed by this project.
