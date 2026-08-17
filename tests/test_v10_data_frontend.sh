#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p build

cc -Iinclude -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
  src/v10_data.c src/media_v10.c tests/test_v10_data_frontend.c \
  -lm -o build/test_v10_data_frontend

./build/test_v10_data_frontend

echo "V10_NATIVE_DATA_STEP2A_STRICT_BUILD=PASS"
