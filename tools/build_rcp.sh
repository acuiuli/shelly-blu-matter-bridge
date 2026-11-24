#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
mkdir -p build
sdk_revision=$(git -C "$IDF_PATH" rev-parse HEAD)
if [[ -d build/h2-rcp ]] && { [[ ! -f build/h2-rcp/.sdk-revision ]] || [[ "$(cat build/h2-rcp/.sdk-revision)" != "$sdk_revision" ]]; }; then
  mv build/h2-rcp "build/h2-rcp.previous.$(date +%s)"
fi
if [[ ! -d build/h2-rcp ]]; then
  cp -a "$IDF_PATH/examples/openthread/ot_rcp" build/h2-rcp
  printf '%s\n' "$sdk_revision" > build/h2-rcp/.sdk-revision
fi
idf.py -C build/h2-rcp -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;$PWD/firmware/rcp.defaults" set-target esp32h2 build
