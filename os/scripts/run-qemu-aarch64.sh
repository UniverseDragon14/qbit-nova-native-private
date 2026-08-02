#!/usr/bin/env bash
#
# Boot the Universal Dragon OS image under QEMU aarch64.
# Requires: qemu-system-aarch64 and a completed build-qemu-aarch64.sh run.
#
# Usage:
#   BUILDROOT_DIR=/path/to/buildroot os/scripts/run-qemu-aarch64.sh
#
# Exit the guest with: Ctrl-a x
#
set -euo pipefail

: "${BUILDROOT_DIR:?set BUILDROOT_DIR to your Buildroot checkout}"

IMG="${BUILDROOT_DIR}/output/images"

exec qemu-system-aarch64 \
	-M virt \
	-cpu cortex-a53 \
	-smp 2 \
	-m 512 \
	-nographic \
	-kernel "${IMG}/Image" \
	-append "root=/dev/vda console=ttyAMA0" \
	-drive file="${IMG}/rootfs.ext4",if=none,format=raw,id=hd0 \
	-device virtio-blk-device,drive=hd0
