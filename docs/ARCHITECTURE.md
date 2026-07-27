# Architecture

```text
.qn source
  -> deterministic lexer
  -> parser / AST
  -> semantic validation
  -> native instruction lowering
  -> portable QBC
  -> QVM
  -> state-vector backend
  -> measurement histogram
  -> evidence receipt
```

## Safety boundary

The VM instruction set contains only quantum-simulation and output operations.
There is no opcode for shell execution, network access, arbitrary file access,
credential handling, persistence, exploitation, or device control.

## Clean-room rule

Historical NOVA and QBIT NOVA implementations may inform externally observable
behavior and tests. Their Python implementation structure is not copied here.
QBIT NOVA C remains a separate frozen Devpost project.
