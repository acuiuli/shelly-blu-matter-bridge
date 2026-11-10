# SPDX-License-Identifier: Apache-2.0
"""Release gates must refuse stale/unreviewed bytes before producing a bundle."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools/package_release.py"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bridge-release-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "inventory"
        self.source.mkdir()
        for name in ("LICENSE", "THIRD_PARTY_NOTICES.md", "toolchain.lock.json", "dependencies.lock"):
            (self.source / name).write_text("fixture\n")
        (self.source / "licenses").mkdir()
        (self.source / "licenses/vendor.txt").write_text("fixture license\n")
        self.app = self.root / "app.bin"
        self.app.write_bytes(b"application fixture")
        self.ota = self.root / "app.ota"
        self.ota.write_bytes(b"reviewed wrapper fixture")
        self.inventory = self.source / "inventory.json"
        self.inventory.write_text(json.dumps({"application": {"path": str(self.app), "sha256": sha(self.app)}}))
        sums = [f"{sha(p)}  {p.relative_to(self.source)}" for p in sorted(self.source.rglob("*")) if p.is_file()]
        (self.source / "SHA256SUMS").write_text("\n".join(sums) + "\n")
        self.review = self.root / "review.json"
        self.review_data = {"inventory_sha256": sha(self.inventory), "ota_image_sha256": sha(self.ota),
                            "reviewer": "Test reviewer", "license_review_complete": True,
                            "ota_metadata_verified": True}
        self.output = self.root / "bundle"

    def run_package(self):
        self.review.write_text(json.dumps(self.review_data))
        return subprocess.run([sys.executable, str(SCRIPT), str(self.source), str(self.review),
                               str(self.ota), str(self.output)], capture_output=True, text=True)

    def test_unreviewed_is_rejected_without_output(self):
        self.review_data["license_review_complete"] = False
        self.assertNotEqual(self.run_package().returncode, 0)
        self.assertFalse(self.output.exists())

    def test_changed_application_is_rejected_without_output(self):
        self.app.write_bytes(b"different bytes")
        self.assertNotEqual(self.run_package().returncode, 0)
        self.assertFalse(self.output.exists())

    def test_changed_license_is_rejected_without_output(self):
        (self.source / "licenses/vendor.txt").write_text("incorrect license text\n")
        self.assertNotEqual(self.run_package().returncode, 0)
        self.assertFalse(self.output.exists())

    def test_bundle_contains_notices_licenses_and_verifiable_checksums(self):
        result = self.run_package()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.output / "licenses/vendor.txt").is_file())
        self.assertEqual((self.output / "LICENSE").read_text(), "fixture\n")
        for line in (self.output / "SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ", 1)
            self.assertEqual(sha(self.output / name), digest)


if __name__ == "__main__":
    unittest.main()
