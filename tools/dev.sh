#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
image=$(python3 -c 'import json; print(json.load(open("tools/toolchain.lock.json"))["image"])')
exec docker run --rm -i --user "$(id -u):$(id -g)" \
  -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e 'GIT_CONFIG_VALUE_0=*' \
  -e HOME=/tmp/bridge-user -v "$PWD:/workspace" -w /workspace "$image" \
  bash -c 'cd /workspace; exec "$@"' -- "$@"
