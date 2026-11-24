# SPDX-License-Identifier: Apache-2.0
"""Fail if an immutable build environment no longer matches the recorded SDKs."""
import json
import importlib.metadata
import os
from pathlib import Path
import subprocess

lock = json.loads(Path("tools/toolchain.lock.json").read_text())
idf = Path(os.environ["IDF_PATH"])
matter = Path(os.environ["ESP_MATTER_PATH"])
paths = {
    "esp_idf": idf,
    "esp_matter": matter,
    "connectedhomeip": matter / "connectedhomeip/connectedhomeip",
    "openthread": idf / "components/openthread/openthread",
    "nimble": idf / "components/bt/host/nimble/nimble",
}
for name, path in paths.items():
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if actual != lock[name]:
        raise SystemExit(f"{name}: expected {lock[name]}, got {actual}")
packages = 0
for line in Path(".devcontainer/python-tools.lock").read_text().splitlines():
    if not line or line.startswith("#"):
        continue
    name, version = line.split("==", 1)
    if importlib.metadata.version(name) != version:
        raise SystemExit(f"Python tool differs from lock: {name}")
    packages += 1
print(f"All five SDK revisions and {packages} Python tool versions match the lock")
