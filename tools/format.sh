#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
mode=${1:---check}
mapfile -t files < <(find core firmware/main simulator tests -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \))
if [[ "$mode" == --check ]]; then
  clang-format --dry-run --Werror "${files[@]}"
else
  clang-format -i "${files[@]}"
fi
