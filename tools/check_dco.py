# SPDX-License-Identifier: Apache-2.0
"""Verify an author DCO sign-off on each commit of a PR, without executing PR text."""
import os
import re
import subprocess

base, head = os.environ["BASE_SHA"], os.environ["HEAD_SHA"]
if not all(re.fullmatch(r"[0-9a-f]{40}", x) for x in (base, head)):
    raise SystemExit("Expected full commit hashes")
commits = subprocess.check_output(["git", "rev-list", f"{base}..{head}"], text=True).splitlines()
errors = []
for commit in commits:
    author, body = subprocess.check_output(["git", "show", "-s", "--format=%an <%ae>%x00%B", commit], text=True).split("\0", 1)
    signoffs = re.findall(r"^Signed-off-by:\s*(.+?)\s*$", body, re.MULTILINE | re.IGNORECASE)
    if author.casefold() not in {s.casefold() for s in signoffs}:
        errors.append(f"{commit}: missing sign-off matching commit author")
if errors:
    raise SystemExit("\n".join(errors))
print(f"Author sign-offs verified on {len(commits)} commits")
