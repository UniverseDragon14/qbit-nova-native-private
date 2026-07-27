# Typed QIR v0.2

Typed QIR is the canonical semantic boundary between the AST and QBC.

## Value types

| Type | Meaning |
|---|---|
| `qbit` | One statically resolved qubit |
| `qreg<N>` | A fixed-width quantum register |
| `result` | A measurement result symbol |

## Operations

| QIR operation | Input | Output |
|---|---|---|
| `H` | `qbit` | updated `qbit` state |
| `X` | `qbit` | updated `qbit` state |
| `Z` | `qbit` | updated `qbit` state |
| `CX` | `qbit, qbit` | updated joint state |
| `MEASURE.ALL` | `qreg<N>` | `result` |
| `EMIT` | `result` | observable output |

## Determinism

The same source, compiler version and options produce byte-identical QBC.

## QBC v2

QBC v2 stores the complete 32-byte source SHA-256 digest. The decoder still
accepts v1 files, whose historical header carried only the first 28 digest
bytes.
