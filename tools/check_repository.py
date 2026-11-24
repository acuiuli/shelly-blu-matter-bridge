# SPDX-License-Identifier: Apache-2.0
"""Offline documentation, private-file, pin and whitespace checks."""
import json
import hashlib
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote

root = Path(__file__).resolve().parents[1]
files = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard"], cwd=root, text=True).splitlines()
errors = []
for name in files:
    p = root / name
    if not p.is_file():
        continue
    if name.startswith((".private/", ".cache/", "secrets/")):
        errors.append(f"Private file in publication list: {name}")
    if p.suffix not in {".md", ".py", ".sh", ".cpp", ".hpp", ".h", ".c", ".yml", ".yaml", ".json", ".txt", ".csv"}:
        continue
    content = p.read_text()
    if not content.endswith("\n") or any(line.rstrip() != line for line in content.splitlines()):
        errors.append(f"Whitespace: {name}")
    if p.suffix == ".md":
        for target in re.findall(r"\[[^\]]*\]\(([^)]+)\)", content):
            if "://" in target or target.startswith("#"):
                continue
            local = unquote(target.split("#", 1)[0])
            if local and not (p.parent / local).exists():
                errors.append(f"Broken local link in {name}: {target}")
    if name.startswith(".github/workflows/"):
        for action in re.findall(r"uses:\s*([^\s]+)", content):
            if not re.fullmatch(r"[^@]+@[0-9a-f]{40}", action):
                errors.append(f"Unpinned Action: {action}")
        if "pull_request_target" in content:
            errors.append(f"Privileged fork trigger: {name}")
        if re.search(r"^\s+- (?:uses|run):", content, re.MULTILINE):
            errors.append(f"Workflow step needs an explicit descriptive name: {name}")
lock = json.loads((root / "tools/toolchain.lock.json").read_text())
container = json.loads((root / ".devcontainer/devcontainer.json").read_text())
args = container["build"]["args"]
if lock["image"] != args["BASE_IMAGE"] or args["ESP_IDF_REV"] != lock["esp_idf"] or not re.search(r"@sha256:[0-9a-f]{64}$", lock["image"]):
    errors.append("Container digest mismatch")
for name, key in (("Dockerfile", "dockerfile_sha256"), ("python-tools.lock", "python_tools_sha256")):
    if hashlib.sha256((root / ".devcontainer" / name).read_bytes()).hexdigest() != lock[key]:
        errors.append(f"Container input differs from lock: {name}")
for patch in lock.get("patches", []):
    if hashlib.sha256((root / patch["path"]).read_bytes()).hexdigest() != patch["sha256"]:
        errors.append(f"SDK patch differs from lock: {patch['path']}")
if errors:
    raise SystemExit("\n".join(errors))
print(f"Repository checks passed ({len(files)} publishable files)")
