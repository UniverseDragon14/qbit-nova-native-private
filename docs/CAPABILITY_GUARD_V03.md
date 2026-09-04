# Capability Guard v0.3

## Purpose

Execution authority is declared by compiled QBC metadata and checked before the
QVM allocates or mutates the state vector.

## Decision states

- `allowed`
- `needs_approval`
- `blocked`

## Deny-by-default rules

Execution is blocked when a capability is:

- explicitly blocked
- unknown
- absent from the selected policy classification

## Approval model

`--approve capability` is an explicit local CLI approval for this process. It
is not a cryptographic signature. Signed approval receipts belong in a later
stage.

## Deterministic diagnostics

- `QN-E-GUARD-001`: blocked capability
- `QN-E-GUARD-002`: unknown capability name
- `QN-E-GUARD-003`: unknown policy profile
- `QN-E-GUARD-004`: invalid guard option
- `QN-E-APPROVAL-001`: approval required
- `QN-E-QBC-CAP-001`: unknown capability bits in QBC

## Security boundary

This document describes the original v0.3 boundary. Stage8 Step1 later adds a
strictly bounded GPIO-output opcode family behind the existing
approval-mandatory `device.control` capability. Shell execution, network
access, arbitrary file writes, credential access, persistence and exploitation
remain unavailable. See `DEVICE_GPIO_V08_STEP1.md` for the current device
boundary.
