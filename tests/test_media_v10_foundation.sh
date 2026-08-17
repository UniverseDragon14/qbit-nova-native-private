#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

CC_BIN="${CC:-cc}"
CFLAGS_STR="${CFLAGS:--std=c17 -O2 -Wall -Wextra -Wpedantic -Werror}"

# shellcheck disable=SC2086
"$CC_BIN" $CFLAGS_STR -I"$ROOT/include" \
  "$ROOT/tests/test_media_v10_foundation.c" \
  "$ROOT/src/media_v10.c" \
  -lm -o "$TMP/test_media_v10_foundation"

"$TMP/test_media_v10_foundation"
echo "V10_MEDIA_FOUNDATION_STRICT_BUILD=PASS"
