# SPDX-License-Identifier: Apache-2.0
"""Build and execute the development image from its immutable base and source locks."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
os.chdir(root)
lock_path = Path("tools/toolchain.lock.json")
lock = json.loads(lock_path.read_text())
recipe = Path(".devcontainer/Dockerfile")
python_lock = Path(".devcontainer/python-tools.lock")
for path, key in ((recipe, "dockerfile_sha256"), (python_lock, "python_tools_sha256")):
    if hashlib.sha256(path.read_bytes()).hexdigest() != lock[key]:
        raise SystemExit(f"{path} changed: refresh the toolchain lock before building")
for patch in lock.get("patches", []):
    if hashlib.sha256(Path(patch["path"]).read_bytes()).hexdigest() != patch["sha256"]:
        raise SystemExit(f"SDK patch differs from lock: {patch['path']}")
fingerprint = hashlib.sha256(lock_path.read_bytes() + recipe.read_bytes() + python_lock.read_bytes()).hexdigest()[:16]
image = f"shelly-blu-bridge-dev:{fingerprint}"
inspect = subprocess.run(["docker", "image", "inspect", image], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
if inspect.returncode:
    subprocess.run(["docker", "build", "--file", str(recipe), "--tag", image,
                    "--build-arg", f"BASE_IMAGE={lock['image']}",
                    "--build-arg", f"ESP_IDF_REV={lock['esp_idf']}", ".devcontainer"], check=True)
if len(sys.argv) == 2 and sys.argv[1] == "--image-name":
    print(image)
    raise SystemExit(0)
command = ["docker", "run", "--rm", "-i", "--user", f"{os.getuid()}:{os.getgid()}",
           "-e", "GIT_CONFIG_COUNT=1", "-e", "GIT_CONFIG_KEY_0=safe.directory", "-e", "GIT_CONFIG_VALUE_0=*",
           "-e", "HOME=/tmp/bridge-user", "-v", f"{root}:/workspace", "-w", "/workspace", image,
           "bash", "-c", 'cd /workspace; exec "$@"', "--", *sys.argv[1:]]
os.execvp(command[0], command)
