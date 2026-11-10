# SPDX-License-Identifier: Apache-2.0
"""Fail if an immutable build environment no longer matches the recorded SDKs."""
import json
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
print("All five SDK revisions match the lock")
