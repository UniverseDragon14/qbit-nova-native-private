#!/bin/sh
# Buildroot post-build hook for Universal Dragon OS.
# Invoked as: post-build.sh $TARGET_DIR [options]
set -e

TARGET_DIR="$1"

# Set the image hostname.
echo "universal-dragon" > "${TARGET_DIR}/etc/hostname"

# Keep /etc/hosts consistent with the hostname.
if [ -f "${TARGET_DIR}/etc/hosts" ]; then
	sed -i 's/^127\.0\.1\.1.*/127.0.1.1\tuniversal-dragon/' \
		"${TARGET_DIR}/etc/hosts" || true
fi
