# SPDX-License-Identifier: Apache-2.0
"""External link maintenance; offline/local links are checked on every PR."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import subprocess
import urllib.error
import urllib.request

files = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "*.md"], text=True).splitlines()
urls = set()
for name in files:
    urls.update(re.findall(r"\]\((https?://[^)]+)\)", Path(name).read_text()))
# Repository access/settings links can legitimately be private or not enabled yet.
urls = {u.split("#")[0] for u in urls if "github.com/acuiuli/" not in u}

def check(url):
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "bridge-link-check/0.1"})
        with urllib.request.urlopen(req, timeout=25) as response:
            response.read(1)
        return None
    except urllib.error.HTTPError as error:
        if error.code in (404, 410):
            return f"Broken ({error.code}): {url}"
        print(f"Unverified ({error.code}): {url}")
    except (OSError, ValueError) as error:
        print(f"Unverified ({error}): {url}")
    return None

with ThreadPoolExecutor(max_workers=4) as pool:
    errors = [e for e in pool.map(check, sorted(urls)) if e]
if errors:
    raise SystemExit("\n".join(errors))
print(f"External links checked: {len(urls)}; access/rate-limit failures reported separately")
