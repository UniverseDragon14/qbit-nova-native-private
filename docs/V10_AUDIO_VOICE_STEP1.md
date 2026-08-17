# QBIT NOVA V10 Native Audio + Voice Foundation — Step 1

## Base checkpoint

- Repository: `UniverseDragon14/qbit-nova-native-private`
- Frozen parent: `f13ccf4e279a792261a5f2cabbd6cd545cc86f0a`
- Frozen Stage7 Step9 is not modified.
- QBC v10 is not implemented or released by this step.

## Step 1 implemented surface

This step introduces an additive, bounded runtime ABI foundation only:

- `f32` runtime value with finite-value validation.
- UTF-8 string view with strict validation and explicit byte length.
- bytes view with explicit byte length and hard bounds.
- typed interleaved normalized `f32` audio buffer descriptor.
- typed voice request descriptor.
- default voice request output: 24 kHz, mono, `f32`.
- no heap ownership transfer inside the ABI.
- malformed UTF-8, invalid pointers, inconsistent audio shapes, unsupported flags, non-finite samples, and out-of-range normalized samples fail closed.

## First foundation request

The C-level ABI proof constructs the request text:

`Vanakkam Aslam`

It validates as a `QN_VALUE_VOICE_REQUEST` with ABI v1, 24 kHz mono `f32` output.

Tamil UTF-8 is also explicitly validated by the test fixture using `வணக்கம் Aslam`.

## Truth boundary

Step 1 does **not** claim:

- lexer support for string/f32/bytes literals,
- AST syntax for voice requests,
- QIR opcodes for voice,
- QBC v10 serialization,
- QVM audio playback,
- TTS synthesis,
- microphone capture,
- Pi5 execution proof,
- release/freeze status.

Those belong to later V10 integration steps. The point of Step 1 is to make the data contract strict before the compiler is allowed to serialize or execute it.

## Local strict proof

Compile and run:

```sh
bash tests/test_media_v10_foundation.sh
```

Expected markers:

```text
QBIT_NOVA_V10_MEDIA_FOUNDATION=PASS
V10_F32=PASS
V10_UTF8_STRING=PASS
V10_BYTES=PASS
V10_TYPED_AUDIO_BUFFER=PASS
V10_VOICE_REQUEST=PASS
V10_VOICE_TEXT_VANAKKAM_ASLAM=PASS
V10_MEDIA_FOUNDATION_STRICT_BUILD=PASS
```

## Frozen compatibility rule

No existing Stage7 Step9 source file is replaced by this step. The new files are additive so the frozen V8/V9 bytecode and Step9 tensor fail-closed behavior remain untouched at source level.
