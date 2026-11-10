#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
mkdir -p build
if [[ ! -d build/h2-rcp ]]; then
  cp -a "$IDF_PATH/examples/openthread/ot_rcp" build/h2-rcp
fi
idf.py -C build/h2-rcp -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;$PWD/firmware/rcp.defaults" set-target esp32h2 build
