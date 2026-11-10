# Contributing

Contributions are Apache-2.0 and must be in English. Start with the capability matrix
or an issue describing a reproducible failure. Useful first tasks include controller
endpoint grouping, BLE protocol fixtures with secrets removed, hardware transport
proof and standard-mapping research. Do not claim real-valve support from simulator tests.

Use `git commit -s` to add a `Signed-off-by` trailer to **each** commit, certifying
[Developer Certificate of Origin 1.1](https://developercertificate.org/). Use your own
identity. Do not sign off on behalf of another person. CI checks that every PR commit
has a matching author sign-off. Rebase/re-sign corrected commits when necessary.

Run the README host tests, `python3 -m unittest discover -s tests -p 'test_*.py'`,
`python3 tools/check_repository.py`, and formatting with
`./tools/dev.sh bash tools/format.sh --check`. For firmware changes run the pinned
cross-build and configuration check. Add behavioral tests for command/state changes;
avoid tests that only restate implementation. Document measurements and exact versions
for hardware claims using the validation template. Retain requested/reported semantics,
per-valve isolation, persistent identities and the standard-clusters-only boundary.

Do not submit keys, PINs, Thread datasets, BLE bonds, full private logs, installation
backups or vendor firmware images. Keep sensitive findings private as in [SECURITY](SECURITY.md).
Maintainers review changes and dependency notices before merge. Breaking endpoint or
persistence changes require a migration proposal and explicit experimental release notes.
