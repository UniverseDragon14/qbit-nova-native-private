# Universal Dragon OS — realistic roadmap

This document scopes "build a new operating system" into stages that can
actually be built and verified, instead of one undifferentiated goal.

## Non-goals

- **No Huawei Pura 70 bootloader unlock.** Huawei discontinued its official
  bootloader-unlock-code service in 2018, and the Pura 70 ships with
  HarmonyOS NEXT, which removed the Android runtime entirely. There is no
  AOSP/GSI/custom-ROM path for that device. This roadmap targets hardware
  with an open boot chain instead (below), not phone bootloader unlocking.
- **Not a from-scratch kernel.** Writing and maintaining a kernel (drivers,
  scheduler, filesystems, network stack) from zero is a multi-year effort
  for a large team. Stage 0 uses the Linux kernel as a base, the same way
  every practical "custom OS" project (Buildroot, Yocto, Alpine, etc.) does.
- **Not physical quantum hardware.** QBIT NOVA Native remains a software
  virtual QCPU (see `docs/ARCHITECTURE.md`); this roadmap does not change
  that boundary.

## What "Universal Dragon OS" means here

A minimal, reproducible Linux-based system image where **QBIT NOVA Native's
capability-approval guard is the security substrate**, not an afterthought:
process execution on the system requires a valid signed approval token,
the same deny-by-default model already implemented in this repo.

## Target hardware

Raspberry Pi 5 (aarch64) and QEMU `virt` aarch64, matching the platform
already used for QBIT NOVA Native verification (see `MANIFEST.json`).
Both are open boot chains with public documentation, unlike the Pura 70.

## Stages

### Stage 0 — Base image
- Pick a minimal Linux base builder: Buildroot (recommended — smaller,
  simpler config surface than Yocto for a single-board target).
- Produce a bootable aarch64 image that reaches a shell on QEMU and on
  Raspberry Pi 5 hardware.
- Exit criteria: signed-off boot log + serial console shell on both targets.

### Stage 1 — QBIT NOVA Native on-board
- Cross-compile `qnova` (this repo) into the Buildroot root filesystem.
- Exit criteria: `qnova run examples/single.qn` succeeds inside the image.

### Stage 2 — Capability-gated init
- Replace ad-hoc shell autologin with a small init-launched supervisor that
  requires a valid `qnova approval verify-ed25519` pass before executing
  any registered system binary (extends the existing deny-by-default guard
  from process launch to system services).
- Exit criteria: an unsigned/expired/tampered binary is refused at boot;
  a validly signed one runs. Both cases covered by an automated test,
  mirroring the negative-test style already in `tests/`.

### Stage 3 — Trust store and revocation wired to system updates
- Use the existing `qn_trust_store` / `qn_revocation_store` /
  `qn_replay_ledger` (already implemented per `docs/NEXT_STAGES.md`) as the
  policy for which issuer keys the image accepts, and to reject replayed or
  revoked update packages.
- Exit criteria: a revoked issuer key or replayed token is rejected during
  a simulated update, with an evidence receipt.

### Stage 4 — Device/network capability adapters
- Implement the `device.control` and `network` capability adapters flagged
  as remaining work in `docs/NEXT_STAGES.md` (item 6), scoped first to
  Raspberry Pi 5 GPIO and a loopback network test, not arbitrary hardware.
- Exit criteria: a `.qn` program requesting `device.control` is blocked
  without an approval and succeeds with one, against real GPIO.

### Stage 5 — Signed, reproducible releases
- Reproducible Buildroot build (pinned package versions, deterministic
  output hash) + signed image manifest, extending item 7 in
  `docs/NEXT_STAGES.md` from source packages to whole-image releases.
- Exit criteria: two independent builds from the same commit produce
  byte-identical images; the release manifest is Ed25519-signed and
  verifies with `qnova`.

## Sequencing note

Each stage above is independently useful and independently testable. None
of it depends on unlocking a Huawei device. If phone deployment is still a
goal later, it should target a device with a documented unlock path (e.g.
Pixel, or Xiaomi/OnePlus models with official unlock tools) rather than the
Pura 70.
