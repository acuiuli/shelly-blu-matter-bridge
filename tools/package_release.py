# SPDX-License-Identifier: Apache-2.0
"""Prepare a local experimental bundle after notice review; never publishes anything."""
import hashlib
import json
from pathlib import Path
import shutil
import sys

if len(sys.argv) != 5:
    raise SystemExit("Usage: package_release.py INVENTORY_DIR REVIEW_JSON OTA_IMAGE OUTPUT_DIR")
source, review_path, ota, output = map(Path, sys.argv[1:])
if output.exists():
    raise SystemExit("Output must be a new directory")

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

inventory_path = source / "inventory.json"
report = json.loads(inventory_path.read_text())
review = json.loads(review_path.read_text())
if (review.get("inventory_sha256") != sha(inventory_path) or
        review.get("license_review_complete") is not True or not review.get("reviewer") or
        review.get("ota_image_sha256") != sha(ota) or review.get("ota_metadata_verified") is not True):
    raise SystemExit("Review must bind this inventory and OTA image, identify reviewer, and confirm notices/metadata")
app = Path(report["application"]["path"])
if sha(app) != report["application"]["sha256"]:
    raise SystemExit("Application changed after inventory collection")
if not ota.is_file() or ota.suffix != ".ota":
    raise SystemExit("Supply the reviewed Matter OTA wrapper (.ota)")
for required in ("LICENSE", "THIRD_PARTY_NOTICES.md", "toolchain.lock.json", "dependencies.lock", "Dockerfile", "python-tools.lock", "SHA256SUMS"):
    if not (source / required).is_file():
        raise SystemExit(f"Inventory is missing {required}")
if not (source / "licenses").is_dir() or not any((source / "licenses").rglob("*")):
    raise SystemExit("Inventory has no collected license texts")
for line in (source / "SHA256SUMS").read_text().splitlines():
    digest, relative = line.split("  ", 1)
    item = (source / relative).resolve()
    if not item.is_relative_to(source.resolve()) or sha(item) != digest:
        raise SystemExit("Collected inventory/notices changed after checksum generation")
rcp = report.get("rcp_application")
if rcp and sha(Path(rcp["path"])) != rcp["sha256"]:
    raise SystemExit("RCP changed after inventory collection")
shutil.copytree(source, output)
shutil.copyfile(review_path, output / "release-review.json")
shutil.copyfile(app, output / "s3-application.bin")
shutil.copyfile(ota, output / "s3-application.ota")
if rcp:
    radio = Path(rcp["path"])
    shutil.copyfile(radio, output / "h2-rcp-usb-only.bin")
(output / "EXPERIMENTAL.txt").write_text(
    "Experimental prototype. No full replacement or certification claim.\n"
    "Hardware, conformance, OTA recovery and full-parity validation remain required.\n"
    "S3 OTA never updates the H2. TRV vendor images are not included.\n")
checksums = [f"{sha(p)}  {p.relative_to(output)}" for p in sorted(output.rglob("*"))
             if p.is_file() and p.name != "SHA256SUMS"]
(output / "SHA256SUMS").write_text("\n".join(checksums) + "\n")
print(f"Prepared local experimental bundle: {output}. No publication performed.")
