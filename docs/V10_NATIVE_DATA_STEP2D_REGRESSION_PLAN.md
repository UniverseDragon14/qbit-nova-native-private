# QBIT NOVA V10 Step 2D — Regression Gate

## Base

- Parent Step2C: `361930e54d0efa663614f5af2a0b6a97021259d5`
- Development branch: `dev/v10-native-data-step2d-regression`
- Frozen Stage7 Step9: `f13ccf4e279a792261a5f2cabbd6cd545cc86f0a`

## Goal

Run the repository's real strict build and full test suite after Step2C main CLI integration before adding more V10 language/runtime surface.

## CI gate

The existing GitHub Actions workflow previously listened only to `main`, while this repository's frozen default branch is `stage7-step9-native-tensor-memory` and V10 work is isolated on development branches. Step2D enables CI for this regression branch without modifying compiler or runtime semantics.

Required runner sequence:

```text
make clean
make
make test
version python_dependency=false check
valgrind safe quantum run
```

`make test` already includes the Step2C V10 CLI regression script, so a green run validates both the existing legacy suite and the new V10 check/qir/build/fail-closed routing tests on the same build.

## Truth boundary

This document is a regression plan, not a PASS certificate. PASS is claimed only after GitHub Actions reports a successful run for the Step2D code commit.

Pi5 proof, QVM V10 data execution, audio playback, TTS, microphone capture, release, and final freeze remain outside this gate.
