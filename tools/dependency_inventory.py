# SPDX-License-Identifier: Apache-2.0
"""Collect build provenance and notices for review; never declares legal completeness."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import yaml  # supplied and pinned by the official SDK container

root = Path.cwd()
out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/dependency-review")
if out.exists():
    raise SystemExit("Choose a new output directory to avoid mixing inventories")
out.mkdir(parents=True)
idf, matter = Path(os.environ["IDF_PATH"]), Path(os.environ["ESP_MATTER_PATH"])
lock = json.loads(Path("tools/toolchain.lock.json").read_text())
project = json.loads(Path("firmware/build/project_description.json").read_text())
registry = yaml.safe_load(Path("firmware/dependencies.lock").read_text())

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def git(path, *args):
    return subprocess.check_output(["git", "-c", f"safe.directory={path}", "-C", str(path), *args], text=True).strip()

licenses = []
seen = set()
def collect(base, prefix):
    for p in sorted(base.rglob("*")):
        if not p.is_file() or ".git" in p.parts:
            continue
        name = p.name.upper()
        if not (name.startswith(("LICENSE", "COPYING", "NOTICE")) or name == "COPYRIGHT"):
            continue
        rel = Path(prefix) / p.relative_to(base)
        if str(rel) in seen:
            continue
        seen.add(str(rel))
        target = out / "licenses" / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, target)
        licenses.append({"path": str(rel), "sha256": sha(p)})

sdks = []
for name, path in (("esp-idf", idf), ("esp-matter", matter)):
    subs = []
    for line in git(path, "submodule", "status", "--recursive").splitlines():
        revision, relative, *_ = line.strip().split()
        subpath = path / relative
        subs.append({"path": relative, "revision": revision.lstrip("-+"),
                     "source": git(subpath, "remote", "get-url", "origin")
                     if not revision.startswith("-") else "uninitialized"})
    sdks.append({"name": name, "revision": git(path, "rev-parse", "HEAD"),
                 "source": git(path, "remote", "get-url", "origin"),
                 "submodules": subs,
                 "local_modifications": bool(git(path, "status", "--porcelain", "--untracked-files=no"))})
    collect(path / "components", f"{name}/components")
    for filename in ("LICENSE", "NOTICE"):
        p = path / filename
        if p.exists():
            target = out / "licenses" / name / filename
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, target)
            licenses.append({"path": f"{name}/{filename}", "sha256": sha(p)})
collect(matter / "connectedhomeip/connectedhomeip", "connectedhomeip")
components = []
for name, item in registry["dependencies"].items():
    if name == "idf":
        continue
    directory = Path("firmware/managed_components") / name.replace("/", "__")
    components.append({"name": name, "version": item["version"],
                       "component_hash": item.get("component_hash"), "source": item["source"],
                       "local_modifications": False})
    candidates = [p for p in directory.iterdir() if p.is_file() and p.name.upper().startswith("LICENSE")]
    text = "\n".join(p.read_text(errors="replace") for p in candidates)
    components[-1]["license_hint"] = ("Apache-2.0" if "Apache License" in text and "Version 2.0" in text
                                      else "MIT" if "MIT License" in text else "See collected license texts")
    collect(directory, f"registry/{name}")
# Preserve map and list the archives actually loaded, including SDK distributed .a files.
map_path = Path("firmware/build/shelly_blu_bridge.map")
if not map_path.exists():
    raise SystemExit("Build must finish before collecting linked library provenance")
archives = []
maps = [("esp32s3", map_path)]
rcp_map = Path("build/h2-rcp/build/esp_ot_rcp.map")
if rcp_map.exists():
    maps.append(("esp32h2", rcp_map))
for target, link_map in maps:
    for raw in sorted(set(re.findall(r"^LOAD (.*\.a)$", link_map.read_text(), re.MULTILINE))):
        p = Path(raw)
        if not p.is_absolute():
            p = root / link_map.parent / p
        archives.append({"target": target, "path": raw, "sha256": sha(p) if p.exists() else None,
                         "notice_review": "required"})
    shutil.copyfile(link_map, out / link_map.name)
for p in (Path("LICENSE"), Path("THIRD_PARTY_NOTICES.md"), Path("firmware/dependencies.lock"), Path("tools/toolchain.lock.json"),
          Path(".devcontainer/Dockerfile"), Path(".devcontainer/python-tools.lock")):
    shutil.copyfile(p, out / p.name)
for patch in lock.get("patches", []):
    target = out / "patches" / Path(patch["path"]).name
    target.parent.mkdir(exist_ok=True)
    shutil.copyfile(patch["path"], target)
report = {"schema": 1, "toolchain": lock, "sdks": sdks, "registry_components": components,
          "application": {"path": "firmware/build/shelly_blu_bridge.bin",
                          "sha256": sha(Path("firmware/build/shelly_blu_bridge.bin"))},
          "build_components": project["build_components"], "loaded_archives": archives,
          "license_files": licenses, "modifications": "Official Matter container extended with pinned ESP-IDF source/tool and Python environment updates; ESP-Matter const log-tag patch recorded in toolchain lock",
          "license_review_complete": False,
          "remaining_review": ["Verify all linked transitive components and embedded headers against notices",
                               "Review distributed binary-library and compiler runtime license obligations",
                               "Review link-map inclusion; a selected component need not be linked"]}
rcp_bin = Path("build/h2-rcp/build/esp_ot_rcp.bin")
if rcp_bin.exists():
    report["rcp_application"] = {"path": str(rcp_bin), "sha256": sha(rcp_bin)}
(out / "inventory.json").write_text(json.dumps(report, indent=2) + "\n")
checksums = [f"{sha(p)}  {p.relative_to(out)}" for p in sorted(out.rglob("*")) if p.is_file()]
(out / "SHA256SUMS").write_text("\n".join(checksums) + "\n")
print(f"Collected {len(components)} registry components, {len(archives)} linked archives, {len(licenses)} license/notice files in {out}")
print("Binary redistribution review remains required")
