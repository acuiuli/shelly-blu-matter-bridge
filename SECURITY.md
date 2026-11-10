# Security policy

Only the latest experimental revision is currently maintained. No production security
support window is offered. Do not use public issues for vulnerabilities or credentials.

Use this repository's **Security → Report a vulnerability** private reporting flow:
[private vulnerability reporting](https://github.com/acuiuli/shelly-blu-matter-bridge/security/advisories/new).
Include affected revision, hardware/software versions, impact and minimal reproduction,
with keys and household identifiers removed. If private reporting is unavailable, use
[GitHub support](https://support.github.com/) to resolve access; do not disclose the
vulnerability publicly as a workaround. No contact email is published.

Maintainers must enable private vulnerability reporting before public launch, aim to
acknowledge reports within seven days, coordinate a fix and advisory with the reporter,
and avoid promising a fix date until triage. These are maintainer targets, not a
service guarantee. Publish affected/fixed versions and recovery instructions with an
advisory after coordinated disclosure.

Production readiness needs review of unique commissioning data, attestation, BLE
bond storage, authenticated OTA, flash/NVS encryption, secret redaction and USB access.
These are open engineering work, not features implied by compiling SDK support.
