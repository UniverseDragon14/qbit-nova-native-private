# Architecture

```text
.qn source
  -> deterministic lexer
  -> parser / AST
  -> semantic validation
  -> native instruction lowering
  -> portable QBC
  -> capability guard / external signed approval
  -> QVM dispatcher
     -> state-vector / native compute backend
     -> bounded GPIO backend (Stage8 Step1)
  -> evidence receipt
```

## Safety boundary

QBC v10 adds exactly three bounded device opcodes: GPIO configuration, one
boolean GPIO write, and device evidence emission. Device execution is denied by
default, requires the approval-gated `device.control` capability, and requires
an explicit mock or Linux GPIO backend. The Linux backend accepts only an
explicit `/dev/gpiochipN` character device, uses the GPIO character-device v2
ABI, and resets the requested line LOW before releasing it.

There is still no opcode for shell execution, network access, arbitrary file
access, credential handling, persistence, motors, relays, PWM, or unbounded
device control. Quantum, scalar, vector, tensor, function, input, and device
modes cannot be mixed in the Stage8 Step1 contract.

## Clean-room rule

Historical NOVA and QBIT NOVA implementations may inform externally observable
behavior and tests. Their Python implementation structure is not copied here.
QBIT NOVA C remains a separate frozen Devpost project.
