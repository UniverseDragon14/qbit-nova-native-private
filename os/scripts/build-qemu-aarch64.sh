#!/usr/bin/env bash
#
# Build the Universal Dragon OS image for QEMU aarch64.
#
# Requirements (run on a Buildroot-capable Linux host, NOT inside Termux):
#   - a Buildroot checkout (git clone https://gitlab.com/buildroot.org/buildroot)
#   - host build tools (build-essential, ~10 GB free disk, network access)
#
# Usage:
#   BUILDROOT_DIR=/path/to/buildroot os/scripts/build-qemu-aarch64.sh
#
set -euo pipefail

: "${BUILDROOT_DIR:?set BUILDROOT_DIR to your Buildroot checkout}"

HERE="$(cd "$(dirname "$0")/.." && pwd)"
EXT="${HERE}/buildroot-external"
BR="${BUILDROOT_DIR}"

# Base config from the user's Buildroot version, then our fragment on top.
make -C "${BR}" BR2_EXTERNAL="${EXT}" qemu_aarch64_virt_defconfig
cat "${EXT}/configs/universal_dragon.fragment" >> "${BR}/.config"
make -C "${BR}" BR2_EXTERNAL="${EXT}" olddefconfig
make -C "${BR}" BR2_EXTERNAL="${EXT}"

echo
echo "Build complete."
echo "  Kernel: ${BR}/output/images/Image"
echo "  Rootfs: ${BR}/output/images/rootfs.ext4"
echo "Boot it with: BUILDROOT_DIR=${BR} os/scripts/run-qemu-aarch64.sh"
