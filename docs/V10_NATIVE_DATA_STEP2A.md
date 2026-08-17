# QBIT NOVA V10 Native Data Step 2A

## Base

- Step 1 parent commit: `0f7a7ad194e685f88e54398e2bda4024ac7300c4`
- Step 2 development branch: `dev/v10-native-data-step2`
- Frozen Stage7 Step9 remains `f13ccf4e279a792261a5f2cabbd6cd545cc86f0a` and is not modified.

## Why Step 2A is a gated lane

QBC v10 does not exist yet. Directly teaching the legacy parser/QBC path about new variable-width constants would create an easy route for accidental V8/V9 serialization. Step 2A therefore proves the native-data frontend contract as a separate, additive V10 lane first.

Pipeline proven by this checkpoint:

```text
V10 source declarations
  -> bounded V10 lexer
  -> typed native-data AST
  -> Step1 f32/string/bytes semantic validators
  -> typed native-data QIR + constant pool
  -> QN-E7818 if legacy QBC emission is attempted
```

## Syntax

```qbit
let gain: f32 = 0.75
let greeting: string = "Vanakkam Aslam"
let packet: bytes = b"QBIT\x00NOVA"
```

Types remain contextual in this lane. No legacy identifier is globally reserved by Step 2A.

## Implemented

- finite `f32` literals, including decimal/exponent form
- UTF-8 string literals with bounded allocation
- bytes literals with `\\xNN` binary escapes
- duplicate declaration rejection
- type/literal mismatch rejection
- malformed UTF-8 rejection through the Step1 media validator
- bounded typed QIR constant pool
- raw IEEE f32 bit preservation in QIR metadata
- explicit `requires_qbc_v10` marker
- fail-closed `QN-E7818` serialization guard

## Truth boundary

This checkpoint does **not** claim:

- integration into the legacy `qn_parse()` entrypoint
- QBC v10 format or serialization
- QVM execution of f32/string/bytes
- string/bytes arithmetic or mutation
- runtime inputs for new V10 types
- functions over new V10 types
- audio/TTS execution
- Pi5 proof
- release or final freeze

The next Step 2 substep can merge this proven type/literal contract into the main QAST/QIR path once the QBC v10 constant-pool record layout is explicitly locked.
