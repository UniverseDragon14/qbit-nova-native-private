# QBIT NOVA V10 Native Data Step 2C — Main CLI Integration

## Parent

- Step 2B parent: `21dced5ef75ea2ceeab6dac3faf75d7eafe2def6`
- Branch: `dev/v10-native-data-step2c-main-integration`
- Frozen Stage7 Step9 remains `f13ccf4e279a792261a5f2cabbd6cd545cc86f0a`.

## Goal

Connect the proven Step2A native-data frontend and Step2B QBC v10 data layout to
the normal `qnova` compiler CLI without changing legacy parser/QIR/QBC behavior.

## Integration architecture

The frozen legacy `src/main.c` is compiled with its entry symbol renamed to
`qn_legacy_main`. A new additive `src/v10_cli_router.c` owns the public `main()`.

```text
qnova command
    |
    +-- source claims f32/string/bytes ---> V10 Step2A parser/QIR
    |                                      -> Step2B QBC v10 encoder
    |
    +-- everything else -----------------> qn_legacy_main(...)
```

This avoids invasive edits to the large legacy command dispatcher and gives the
V10 path an explicit compatibility boundary.

## Enabled commands for V10 native-data source

- `qnova check file.qn`
- `qnova qir file.qn`
- `qnova build file.qn -o file.qbc`

`build` produces canonical QBC v10 data-only output using the Step2B locked
layout.

## Fail-closed runtime boundary

The QVM does not execute V10 native-data yet.

- `qnova run` on a V10 native-data source fails with `QN-E7831`.
- `qnova exec` on a QBC v10 file fails with `QN-E7832`.

The router checks the QBC magic/version before the legacy decoder can interpret
the file, so V10 data cannot accidentally enter the V1-V9 VM path.

## Compatibility design

- Legacy `src/main.c` source is not edited by Step2C.
- Legacy `qn_compile()` is not modified.
- Legacy `qn_qbc_encode()` / `qn_qbc_decode()` are not modified.
- Legacy source delegates to `qn_legacy_main`.
- Frozen Step9 remains untouched.

## Test surface added

`tests/test_v10_step2c_cli.sh` covers:

1. V10 `check` routing.
2. V10 typed QIR routing.
3. V10 normal CLI build.
4. V10 QBC `exec` fail-closed.
5. V10 source `run` fail-closed.
6. A legacy QBC v9 build still travelling through the legacy path.

The script is wired into `make test` for later repository/CI/Pi5 execution.

## README

The root README now documents the V10 development pipeline, `Hi bro 😊`
example, normal CLI build commands, QBC v10 format status, and the current QVM
execution boundary.

## Truth boundary

This commit implements the Step2C integration and authors its regression test,
but this turn does not claim a full repository regression or Pi5 proof unless a
separate run is performed. QBC v10 remains a development format; QVM V10 data
execution, audio playback, TTS, microphone capture, release, and final freeze are
not claimed.
