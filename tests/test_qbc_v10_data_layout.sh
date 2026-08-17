#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p build

cc -Iinclude -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
  tests/test_qbc_v10_data_layout.c \
  src/qbc_v10_data.c src/media_v10.c src/util.c \
  -lm -o build/test_qbc_v10_data_layout

./build/test_qbc_v10_data_layout
printf '%s\n' 'V10_QBC_STEP2B_STRICT_BUILD=PASS'
