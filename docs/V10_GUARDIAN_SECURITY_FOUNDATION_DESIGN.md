# QBIT NOVA V10 Guardian + Ethical Security Foundation

Status: DESIGN FOUNDATION
Base: 7e5ce96933e171d4f759f0b6a6205710580236b0

## Scope

This step is additive only.

It does not modify:
- frozen QBC V8/V9 behavior
- frozen Step9 tensor behavior
- QBC V10 PROGRAM ABI
- physical-QPU claims

## Guardian Sense capabilities

- sensor.rf.observe
- sensor.camera.detect
- network.passive.observe

Guardian Sense reports evidence and confidence only.

It must never claim that a person is definitely watching the user unless independently proven.

No covert person tracking.
No face-based stranger tracking.
No hidden surveillance collection.

## Ethical Security capabilities

- security.scan
- security.audit
- security.fuzz
- security.validate
- security.exploit.lab

security.exploit.lab requires:
- explicit owner approval
- isolated lab scope
- bounded execution
- no persistence
- no credential theft
- no destructive action
- no unauthorized target

## Hard-denied behavior

- credential theft
- persistence
- destructive actions
- unauthorized scanning
- covert person tracking
- surveillance of third parties
- physical-QPU claims

## Execution model

request
-> capability classification
-> scope validation
-> approval decision
-> bounded operation
-> evidence
-> deterministic receipt

Unknown or unclassified actions fail closed.

## Foundation rule

This step introduces C-level contracts first.

No new V10 PROGRAM opcode or executable QBC format is introduced by this foundation.
