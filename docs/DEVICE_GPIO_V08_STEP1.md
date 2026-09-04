# Stage8 Step1: bounded GPIO output

## Status and truth boundary

This isolated branch adds a complete, bounded GPIO-output path. Portable build,
contract, tamper, approval and mock-execution tests are automated. A mock run is
not physical GPIO proof. Physical Pi5 evidence exists only after the Linux GPIO
backend is run on the intended Pi, gpiochip and wired line.

## Source contract

```qn
requires device.control
device led0 gpio pin = 21
write led0 high
emit led0
```

`pin` is a Linux GPIO character-device **line offset**. It is not a physical
40-pin header number. Stage8 Step1 permits exactly:

- one explicit `requires device.control` declaration;
- one GPIO output declaration with line offset `0..63`;
- one `high` or `low` write to that device;
- one emit of the same device;
- no mixing with quantum, vector, scalar, tensor, input or function programs.

The new words are contextual rather than globally reserved. Existing valid
identifiers named `device`, `write`, `high`, or `low` remain valid.

## Compiler and runtime contract

The compiler lowers the source to `GPIO.CONFIG`, `GPIO.WRITE`, `DEVICE.EMIT`
and `END`. Device bytecode uses deterministic QBC v10 (104-byte header) and is
accepted only when the complete four-instruction contract and the exact
`evidence.emit,device.control` capability set validate.

Execution order is:

1. Verify external approval and its source/capability/expiry binding.
2. Check token and issuer revocation.
3. Enforce `device.control` through the existing guard.
4. Validate backend and all device options before replay-token consumption.
5. Atomically consume replay state.
6. Execute the bounded device operation.
7. Reset the line LOW, release it and emit evidence.

The source cannot approve itself. `approve led0` is intentionally not syntax.

## Backends

| Backend | Hardware mutation | Purpose |
|---|---:|---|
| `deny` (default) | No | Fail closed unless an operator explicitly selects execution |
| `mock` | No | Parser/QIR/QBC/guard/receipt and CI proof |
| `linux-gpio` | Yes | Linux GPIO character-device v2 output on an explicit `/dev/gpiochipN`; Ed25519 approval required |

A HIGH write is held for `0..5000` ms and then reset LOW. The default is 250
ms. A LOW write is also completed with a LOW final state. Persistent outputs,
PWM, sensors, relays and motors are outside this step.

## Portable proof

```bash
make
./build/test_device_gpio
bash tests/test_device_gpio.sh
```

The integration test proves deterministic QBC v10, malformed-bytecode
rejection, approval RED/GREEN, preflight-before-replay, source/QBC execution
equivalence, JSON evidence and mock non-mutation.

## Pi5 physical verification

First identify the correct chip and line using the Pi's GPIO documentation or
`gpioinfo`. Confirm the line is free and wire only a 3.3 V-compatible LED with
an appropriate series resistor. Never drive a relay, motor or 5 V load directly
from a Pi GPIO.

After generating and issuing a short-lived Ed25519 `device.control` approval,
run the approved source with explicit hardware selection:

```bash
./build/qnova run examples/device_gpio_led.qn \
  --signed-approval-file device.qns \
  --approval-public-key-file device.pub \
  --replay-ledger-file device-replay.qnrl \
  --revocation-store-file revocation.qnrv \
  --device-backend linux-gpio \
  --gpiochip /dev/gpiochipN \
  --device-hold-ms 250 \
  --receipt device-pi5.json
```

Use normal device permissions rather than running the entire service as root.
The receipt must show `physical_device=true`, `write_executed=true`, and
`safety_reset_low=true`. Keep those claims out of public documentation until
the physical run and wiring have been independently checked.
