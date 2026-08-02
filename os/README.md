# Universal Dragon OS — build tree

This directory turns "build a new OS" into something concrete and buildable:
a **Buildroot external tree** that produces a minimal aarch64 Linux image with
**QBIT NOVA Native (`qnova`) installed as the capability-security substrate**.

It implements Stage 0 + Stage 1 of `docs/UNIVERSAL_DRAGON_OS_ROADMAP.md`:

- Stage 0 — a bootable aarch64 image (QEMU `virt`, and Raspberry Pi 5).
- Stage 1 — `qnova` cross-compiled into the root filesystem.

There is **no Huawei bootloader unlock here** — the Pura 70 has no unlock path
(see the roadmap). This targets open hardware instead.

## Layout

```
os/
  buildroot-external/            BR2_EXTERNAL tree (name: UNIVERSAL_DRAGON)
    external.desc                external tree descriptor
    Config.in / external.mk      wires the qnova package into menuconfig
    package/qnova/               qnova package (builds from this repo)
    configs/universal_dragon.fragment   config merged onto a base defconfig
    board/universal-dragon/      rootfs overlay (os-release, issue) + hooks
  scripts/
    build-qemu-aarch64.sh        base defconfig + fragment -> full image
    run-qemu-aarch64.sh          boot the image under qemu-system-aarch64
    verify-qnova-aarch64.sh      quick aarch64 cross + qemu-user smoke test
```

## Build (on a Buildroot-capable Linux host)

This is **not** run inside Termux on the phone — a full Buildroot build needs a
desktop/server Linux host with a toolchain, ~10 GB free disk, and network.

```bash
git clone https://gitlab.com/buildroot.org/buildroot ~/buildroot
BUILDROOT_DIR=~/buildroot ./os/scripts/build-qemu-aarch64.sh
BUILDROOT_DIR=~/buildroot ./os/scripts/run-qemu-aarch64.sh   # Ctrl-a x to exit
```

At the login prompt the banner reads *Universal Dragon OS* and `qnova version`
runs from `/usr/bin/qnova`.

### Raspberry Pi 5

Use `raspberrypi5_defconfig` as the base instead of `qemu_aarch64_virt_defconfig`
(swap the defconfig name in `build-qemu-aarch64.sh`), then flash
`output/images/sdcard.img` to an SD card.

## What has and has not been verified here

The build proxy in this session blocks `buildroot.org` and has no
`qemu-system-aarch64`, so the **full image build/boot has not been run here** —
it is meant to run on your host.

What **was** verified in-session, against real aarch64:

- `qnova` builds clean on the host with the strict project flags
  (`-std=c17 -Wall -Wextra -Wpedantic -Werror`) and `make test` passes.
- The replay-ledger unit test cross-compiles clean for aarch64 and passes
  repeatedly under `qemu-aarch64-static` — reproduce with
  `os/scripts/verify-qnova-aarch64.sh`.

## Notes

- `package/qnova` uses `SITE_METHOD = local` pointing at the repository root,
  so the image always builds the current source tree (offline-friendly).
- The package drops `-Werror` for the cross build (a different toolchain
  version's warnings should not fail an image build) but keeps `-std=c17`.
