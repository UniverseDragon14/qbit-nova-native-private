#!/usr/bin/env bash
#
# Lightweight aarch64 sanity check WITHOUT a full Buildroot build.
#
# Cross-compiles the OpenSSL-free replay-ledger unit test for aarch64 and runs
# it under qemu-user. Proves the C17 sources compile clean for aarch64 and the
# concurrency-sensitive replay ledger behaves correctly on the target ABI.
#
# Requirements (Debian/Ubuntu host):
#   apt-get install gcc-aarch64-linux-gnu libc6-dev-arm64-cross qemu-user-static
#
# Usage:
#   os/scripts/verify-qnova-aarch64.sh
#
set -euo pipefail

CC="${CC:-aarch64-linux-gnu-gcc}"
QEMU="${QEMU:-qemu-aarch64-static}"
RUNS="${RUNS:-8}"

HERE="$(cd "$(dirname "$0")/../.." && pwd)"   # repository root
OUT="$(mktemp -d)"
trap 'rm -rf "${OUT}"' EXIT

"${CC}" -I"${HERE}/include" \
	-std=c17 -O2 -Wall -Wextra -Wpedantic -Werror -static \
	"${HERE}/tests/test_replay_ledger.c" \
	"${HERE}/src/replay_ledger.c" \
	"${HERE}/src/util.c" \
	-o "${OUT}/rl_aarch64" -lm

for i in $(seq 1 "${RUNS}"); do
	"${QEMU}" "${OUT}/rl_aarch64" >/dev/null
done

echo "OK: replay-ledger test passes on aarch64 (qemu-user, ${RUNS} runs)"
