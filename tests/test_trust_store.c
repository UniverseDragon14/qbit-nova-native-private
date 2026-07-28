#include "qn_trust_store.h"

#include <stdio.h>
#include <string.h>

static void make_key(
    uint8_t key[QN_ED25519_PUBLIC_KEY_BYTES],
    uint8_t marker
) {
    memset(key, 0, QN_ED25519_PUBLIC_KEY_BYTES);
    key[0] = marker;
    key[QN_ED25519_PUBLIC_KEY_BYTES - 1u] = 0xa5u;
}

int main(void) {
    QNTrustStore store;
    QNTrustStore capacity_store;
    QNDiagnostic diag = {0};

    uint8_t key_a[QN_ED25519_PUBLIC_KEY_BYTES];
    uint8_t key_b[QN_ED25519_PUBLIC_KEY_BYTES];
    uint8_t key_extra[QN_ED25519_PUBLIC_KEY_BYTES];
    uint8_t zero_key[QN_ED25519_PUBLIC_KEY_BYTES] = {0};

    uint8_t fingerprint_a[QN_ED25519_FINGERPRINT_BYTES];
    uint8_t fingerprint_b[QN_ED25519_FINGERPRINT_BYTES];
    uint8_t resolved[QN_ED25519_PUBLIC_KEY_BYTES];
    uint8_t unchanged[QN_ED25519_PUBLIC_KEY_BYTES];

    char label_63[QN_TRUST_STORE_LABEL_MAX];
    char label_64[QN_TRUST_STORE_LABEL_MAX + 1u];
    char control_label[] = {'b', 'a', 'd', '\n', '\0'};
    char non_ascii_label[] = {'b', 'a', 'd', (char)0x80, '\0'};

    make_key(key_a, 1u);
    make_key(key_b, 2u);
    make_key(key_extra, 250u);

    qn_trust_store_init(NULL);
    qn_trust_store_init(&store);

    if (store.count != 0u) {
        puts("FAIL: TRUST_STORE_INIT_COUNT");
        return 1;
    }

    if (qn_trust_store_contains(NULL, fingerprint_a) ||
        qn_trust_store_contains(&store, NULL)) {
        puts("FAIL: NULL_LOOKUP_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    if (qn_trust_store_add(
            NULL,
            key_a,
            "creator-primary",
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5101") != 0) {
        puts("FAIL: NULL_STORE_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    if (qn_trust_store_add(
            &store,
            NULL,
            "creator-primary",
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5101") != 0) {
        puts("FAIL: NULL_KEY_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    if (qn_trust_store_add(
            &store,
            key_a,
            NULL,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5101") != 0) {
        puts("FAIL: NULL_LABEL_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    if (qn_trust_store_add(
            &store,
            key_a,
            "",
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5101") != 0) {
        puts("FAIL: EMPTY_LABEL_ACCEPTED");
        return 1;
    }

    memset(label_63, 'a', sizeof(label_63) - 1u);
    label_63[sizeof(label_63) - 1u] = '\0';

    if (qn_trust_store_add(
            &store,
            key_a,
            label_63,
            &diag) != QN_OK) {
        puts("FAIL: LABEL_63_REJECTED");
        return 1;
    }

    if (store.count != 1u) {
        puts("FAIL: TRUST_STORE_ADD_COUNT");
        return 1;
    }

    memset(label_64, 'b', sizeof(label_64) - 1u);
    label_64[sizeof(label_64) - 1u] = '\0';

    memset(&diag, 0, sizeof(diag));
    if (qn_trust_store_add(
            &store,
            key_b,
            label_64,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5101") != 0) {
        puts("FAIL: LABEL_64_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    if (qn_trust_store_add(
            &store,
            key_b,
            control_label,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5101") != 0) {
        puts("FAIL: CONTROL_LABEL_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    if (qn_trust_store_add(
            &store,
            key_b,
            non_ascii_label,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5101") != 0) {
        puts("FAIL: NON_ASCII_LABEL_ACCEPTED");
        return 1;
    }

    if (qn_trust_store_add(
            &store,
            key_b,
            "creator-secondary",
            &diag) != QN_OK) {
        puts("FAIL: SECOND_ISSUER_ADD");
        return 1;
    }

    qn_ed25519_fingerprint(key_a, fingerprint_a);
    qn_ed25519_fingerprint(key_b, fingerprint_b);

    if (!qn_trust_store_contains(&store, fingerprint_a) ||
        !qn_trust_store_contains(&store, fingerprint_b)) {
        puts("FAIL: TRUSTED_ISSUER_NOT_FOUND");
        return 1;
    }

    memset(resolved, 0, sizeof(resolved));
    memset(&diag, 0, sizeof(diag));

    if (qn_trust_store_resolve(
            &store,
            fingerprint_a,
            resolved,
            &diag) != QN_OK ||
        memcmp(resolved, key_a, sizeof(key_a)) != 0) {
        puts("FAIL: TRUSTED_KEY_A_RESOLUTION");
        return 1;
    }

    memset(resolved, 0, sizeof(resolved));

    if (qn_trust_store_resolve(
            &store,
            fingerprint_b,
            resolved,
            &diag) != QN_OK ||
        memcmp(resolved, key_b, sizeof(key_b)) != 0) {
        puts("FAIL: TRUSTED_KEY_B_RESOLUTION");
        return 1;
    }

    size_t count_before_duplicate = store.count;

    memset(&diag, 0, sizeof(diag));
    if (qn_trust_store_add(
            &store,
            key_a,
            "duplicate",
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5102") != 0 ||
        store.count != count_before_duplicate) {
        puts("FAIL: DUPLICATE_ISSUER_HANDLING");
        return 1;
    }

    uint8_t unknown_fingerprint[QN_ED25519_FINGERPRINT_BYTES];
    qn_ed25519_fingerprint(key_extra, unknown_fingerprint);

    memset(resolved, 0x5a, sizeof(resolved));
    memset(unchanged, 0x5a, sizeof(unchanged));
    memset(&diag, 0, sizeof(diag));

    if (qn_trust_store_resolve(
            &store,
            unknown_fingerprint,
            resolved,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5104") != 0 ||
        memcmp(resolved, unchanged, sizeof(resolved)) != 0) {
        puts("FAIL: UNTRUSTED_ISSUER_RESOLUTION");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    if (qn_trust_store_add(
            &store,
            zero_key,
            "zero-key",
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5101") != 0) {
        puts("FAIL: ZERO_PUBLIC_KEY_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));
    if (qn_trust_store_resolve(
            NULL,
            fingerprint_a,
            resolved,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5101") != 0) {
        puts("FAIL: NULL_RESOLVE_STORE_ACCEPTED");
        return 1;
    }

    qn_trust_store_init(&capacity_store);

    for (size_t index = 0u;
         index < QN_TRUST_STORE_MAX_ISSUERS;
         ++index) {
        uint8_t key[QN_ED25519_PUBLIC_KEY_BYTES];
        char label[32];

        make_key(key, (uint8_t)(index + 1u));

        snprintf(
            label,
            sizeof(label),
            "issuer-%02zu",
            index
        );

        memset(&diag, 0, sizeof(diag));

        if (qn_trust_store_add(
                &capacity_store,
                key,
                label,
                &diag) != QN_OK) {
            puts("FAIL: CAPACITY_ENTRY_REJECTED");
            return 1;
        }
    }

    if (capacity_store.count !=
        QN_TRUST_STORE_MAX_ISSUERS) {
        puts("FAIL: CAPACITY_COUNT");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_trust_store_add(
            &capacity_store,
            key_extra,
            "issuer-over-limit",
            &diag) != QN_ERR_LIMIT ||
        strcmp(diag.code, "QN-E5103") != 0 ||
        capacity_store.count !=
            QN_TRUST_STORE_MAX_ISSUERS) {
        puts("FAIL: STORE_LIMIT_NOT_ENFORCED");
        return 1;
    }

    puts("PASS: QBIT_NOVA_TRUST_STORE_API_V052_STEP1_HARDENED");
    return 0;
}
