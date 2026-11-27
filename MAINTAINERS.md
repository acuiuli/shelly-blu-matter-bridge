# Maintainers and governance

Repository owner and initial code owner: [@acuiuli](https://github.com/acuiuli).
Contributors retain copyright in their work. The maintainer reviews design boundaries,
DCO checks, tests, dependency changes, reports, releases and security coordination.
Changes should be reviewable as a pull request; require CI and code-owner review in
branch protection before accepting external contributions.

The owner maintains private vulnerability reporting, the conduct reporting links,
and the required checks on the default branch. Review source history and release
contents for installation data before publishing artifacts. The original Italian draft
is kept locally under ignored `.private/`; it must never be staged, archived or pushed.
The initial tracked history contained only LICENSE. Do not use `git add -f` to override
private-file exclusions. Public visibility and GitHub repository settings are separate
hosting actions; the local foundation does not change those settings automatically.

Keep experimental status until every parity row and validation gate passes. Publish
unsupported findings openly. Never publish a complete-replacement claim based on a
partial mapping or gateway OTA alone. Handle maintainer succession through a documented
GitHub PR and update CODEOWNERS and private security access together.
