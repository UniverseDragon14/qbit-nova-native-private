# Completed foundations

- Native C17 lexer, parser, AST and QVM
- Typed QIR and deterministic QBC
- Capability derivation and deny-by-default guard
- HMAC-SHA-256 authenticated approvals
- OpenSSL EVP Ed25519 public-key approvals
- Canonical binary signed token
- Stable capability IDs
- Exact source, capability and expiry binding
- Issuer and token digests in execution receipts
- Negative tamper and malformed-token tests
- GitHub Actions CI definition
- Trusted issuer store, replay ledger and token/issuer revocation
- CPU/Vulkan routing and bounded V3D vector compute
- Typed u32 arithmetic, comparison, if/else, bounded repeat and functions
- Runtime u32 inputs and deterministic QBC v9
- Stage8 Step1 isolated bounded GPIO output and QBC v10

# Remaining stages

1. Physical Pi5 GPIO evidence on the explicitly selected gpiochip/line
2. GPIO input with debounce and read-only capability separation
3. Time-bounded device leases and watchdog recovery
4. Encrypted PKCS#8 private-key support
5. ARM NEON scalar-equivalence backend
6. Tensor and model-routing QIR beyond the frozen metadata frontend
7. Signed packages and reproducible releases
