# Release conventions

Use `v0.MINOR.PATCH-experimental.N` tags until all full-parity gates pass. A source or
prototype release must say experimental prominently and list unsupported capabilities.
Matter software version numbers increase monotonically independently of the display
version. Never reuse a software version number for different bytes. Endpoint/persistence
changes require migration notes before release. Do not tag or publish automatically from
fork pull requests. Only a maintainer publishes after reviewing the concrete bundle.

Include source revision, toolchain digest, S3 and H2 revisions, supported hardware,
validation reports, known limitations and recovery instructions. Binary bundles need
LICENSE, THIRD_PARTY_NOTICES, the actual dependency inventory (including transitive
components and linked binary archives), applicable license/NOTICE texts, image metadata
and SHA256SUMS. Vendor TRV images require separate permission and are not bundled.

`tools/dependency_inventory.py` collects actual SDK submodules, resolved registry locks,
license/NOTICE texts and binary-library hashes from the build. It deliberately marks
license review incomplete until a maintainer checks the link map against the inventory
and records obligations for redistributed binary libraries. Do not treat the collector
as a legal determination. A compile artifact may be inspected privately; it is not an
automatically approved public binary release. Do not publish binary bundles while
notice/license review or required recovery tests remain incomplete.

Before public repository launch, confirm [maintainer setup](../MAINTAINERS.md). Use a
source allowlist/`git archive`, which excludes ignored `.private` and build outputs;
never archive the whole working directory. Inspect all historical refs before pushing.

After review, a local bundle can be prepared with `tools/package_release.py`. Supply
an inventory directory, a private review JSON, a Matter `.ota` wrapper and a new output
directory. The review JSON must bind `inventory_sha256` and `ota_image_sha256`, identify
`reviewer`, and confirm `license_review_complete: true` and `ota_metadata_verified: true`.
The script verifies the S3/RCP hashes, includes notices/licenses/inventory, and computes
bundle checksums. It does not validate conformance or publish a release; a review flag
is a human assertion and must be backed by the actual review. No reviewed wrapper or
review approval exists for this foundation, so no binary bundle has been produced.
