# Dragon Voice V3 — Smile/Laugh POC

This experiment keeps the frozen QBIT NOVA Native branch untouched and proves the **small control packet** idea for expressive voice.

## Goal

A large conversation or model state should not be pushed around as the voice control surface. QBIT NOVA emits a tiny deterministic packet that tells the downstream renderer *how* to speak.

Current proof packet:

1. protocol version
2. emotion id
3. smile amount
4. laugh amount
5. warmth
6. energy
7. pace

Each field is one `u32`, so the semantic control payload is only **28 bytes** before any transport framing.

## Build on Termux / Raspberry Pi 5

From the repository root:

```bash
git checkout dragon-voice-v3-smile-poc
make clean
make
./build/qnova run experiments/dragon_voice_v3/smile_laugh_packet.qn
```

Expected semantic values, in order:

```text
3
1
82
38
88
64
54
```

The exact console formatting is owned by the current QNOVA runtime; the values above are the contract.

## Important truth boundary

This commit does **not** pretend that a natural laugh waveform already exists in QBIT NOVA Native. The current language/runtime can deterministically produce the expressive control packet. A separate Dragon Voice renderer must convert that packet plus text/reference voice state into audio.

That renderer should be local-first on Pi5 and must not require a paid API for the core path.

## Next renderer contract

The future renderer should accept:

```text
text/ref -> Dragon Voice renderer
control  -> { emotion, smile, laugh, warmth, energy, pace }
result   -> PCM/WAV stream
```

For the first real listening test, use the existing Dragon Voice reference assets (for example the Smile/Happy/Playful recordings) as voice identity references, not as random clips to be played back.
