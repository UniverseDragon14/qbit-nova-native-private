#include "qn_qbc_v10_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static uint16_t g16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t g32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void p32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

int main(void) {
    QNV10DataQIRProgram q = {0};
    q.abi_version = QN_V10_DATA_ABI_V1;
    q.value_count = 3u;
    q.requires_qbc_v10 = true;

    snprintf(q.values[0].name, sizeof(q.values[0].name), "gain");
    q.values[0].kind = QN_VALUE_F32;
    q.values[0].constant_offset = UINT32_MAX;
    q.values[0].byte_length = 4u;
    float f = 0.75f;
    memcpy(&q.values[0].f32_bits, &f, sizeof(f));

    static const uint8_t greeting[] = "Hi bro \xF0\x9F\x98\x8A";
    static const uint8_t packet[] = {'Q','B','I','T',0,'N','O','V','A'};
    q.constant_bytes_size = (uint32_t)((sizeof(greeting) - 1u) + sizeof(packet));
    q.constant_bytes = (uint8_t *)malloc(q.constant_bytes_size);
    if (!q.constant_bytes) return fail("alloc");
    memcpy(q.constant_bytes, greeting, sizeof(greeting) - 1u);
    memcpy(q.constant_bytes + sizeof(greeting) - 1u, packet, sizeof(packet));

    snprintf(q.values[1].name, sizeof(q.values[1].name), "greeting");
    q.values[1].kind = QN_VALUE_STRING;
    q.values[1].constant_offset = 0u;
    q.values[1].byte_length = (uint32_t)(sizeof(greeting) - 1u);

    snprintf(q.values[2].name, sizeof(q.values[2].name), "packet");
    q.values[2].kind = QN_VALUE_BYTES;
    q.values[2].constant_offset = q.values[1].byte_length;
    q.values[2].byte_length = (uint32_t)sizeof(packet);

    uint8_t digest[32];
    for (unsigned i = 0u; i < 32u; ++i) digest[i] = (uint8_t)i;
    QNDiagnostic diag = {0};
    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;

    if (qn_qbc_v10_data_encode(&q, digest, &encoded, &encoded_size, &diag) != QN_OK) {
        free(q.constant_bytes);
        return fail(diag.message);
    }
    if (encoded_size != QN_QBC_V10_HEADER_SIZE +
                        3u * QN_QBC_V10_VALUE_RECORD_SIZE +
                        q.constant_bytes_size) return fail("size");
    if (memcmp(encoded, "QBCN", 4u) != 0 ||
        g16(encoded + 4u) != 10u || g16(encoded + 6u) != 128u) {
        return fail("header identity");
    }
    if (g16(encoded + 104u) != 3u || g16(encoded + 106u) != 80u ||
        g32(encoded + 112u) != 128u || g32(encoded + 116u) != 368u) {
        return fail("section offsets");
    }

    QNV10DataQIRProgram out = {0};
    uint8_t digest2[32];
    if (qn_qbc_v10_data_decode(encoded, encoded_size, &out, digest2, &diag) != QN_OK) {
        return fail(diag.message);
    }
    if (memcmp(digest, digest2, 32u) != 0 ||
        out.value_count != 3u || out.constant_bytes_size != q.constant_bytes_size) {
        return fail("round trip metadata");
    }
    if (out.values[0].f32_bits != q.values[0].f32_bits ||
        strcmp(out.values[1].name, "greeting") != 0 ||
        memcmp(out.constant_bytes, q.constant_bytes, q.constant_bytes_size) != 0) {
        return fail("round trip content");
    }
    free(out.constant_bytes);
    memset(&out, 0, sizeof(out));

    uint32_t normal_f32_bits = q.values[0].f32_bits;
    q.values[0].f32_bits = UINT32_C(0x80000000);
    uint8_t *negative_zero_encoded = NULL;
    size_t negative_zero_size = 0u;
    if (qn_qbc_v10_data_encode(&q, digest, &negative_zero_encoded,
                               &negative_zero_size, &diag) != QN_OK) {
        return fail("negative zero encode rejected instead of canonicalized");
    }
    if (g32(negative_zero_encoded + QN_QBC_V10_HEADER_SIZE + 76u) != 0u) {
        return fail("negative zero not canonicalized to positive zero");
    }
    if (qn_qbc_v10_data_decode(negative_zero_encoded, negative_zero_size,
                               &out, NULL, &diag) != QN_OK) {
        return fail("canonicalized negative zero did not decode");
    }
    if (out.values[0].f32_bits != 0u) return fail("decoded zero bits not canonical");
    free(out.constant_bytes);
    memset(&out, 0, sizeof(out));
    free(negative_zero_encoded);
    q.values[0].f32_bits = normal_f32_bits;

    uint8_t saved_string_byte = q.constant_bytes[0];
    q.constant_bytes[0] = 0u;
    uint8_t *nul_encoded = NULL;
    size_t nul_encoded_size = 0u;
    if (qn_qbc_v10_data_encode(&q, digest, &nul_encoded,
                               &nul_encoded_size, &diag) == QN_OK) {
        free(nul_encoded);
        return fail("string NUL accepted by encoder");
    }
    if (nul_encoded != NULL || nul_encoded_size != 0u) {
        return fail("failed encoder published output");
    }
    q.constant_bytes[0] = saved_string_byte;

    uint8_t *bad = (uint8_t *)malloc(encoded_size);
    if (!bad) return fail("bad alloc");

    memcpy(bad, encoded, encoded_size);
    bad[4u] = 11u;
    if (qn_qbc_v10_data_decode(bad, encoded_size, &out, NULL, &diag) == QN_OK) {
        return fail("bad version accepted");
    }

    memcpy(bad, encoded, encoded_size);
    bad[106u] = 79u;
    if (qn_qbc_v10_data_decode(bad, encoded_size, &out, NULL, &diag) == QN_OK) {
        return fail("bad record size accepted");
    }

    memcpy(bad, encoded, encoded_size);
    bad[116u] = 0u;
    if (qn_qbc_v10_data_decode(bad, encoded_size, &out, NULL, &diag) == QN_OK) {
        return fail("bad pool offset accepted");
    }

    memcpy(bad, encoded, encoded_size);
    p32(bad + QN_QBC_V10_HEADER_SIZE + 68u, 0u);
    if (qn_qbc_v10_data_decode(bad, encoded_size, &out, NULL, &diag) == QN_OK) {
        return fail("bad f32 sentinel accepted");
    }

    memcpy(bad, encoded, encoded_size);
    p32(bad + QN_QBC_V10_HEADER_SIZE + 76u, UINT32_C(0x80000000));
    if (qn_qbc_v10_data_decode(bad, encoded_size, &out, NULL, &diag) == QN_OK) {
        return fail("noncanonical negative zero accepted");
    }

    memcpy(bad, encoded, encoded_size);
    bad[QN_QBC_V10_HEADER_SIZE + QN_QBC_V10_VALUE_RECORD_SIZE + 64u] = 99u;
    if (qn_qbc_v10_data_decode(bad, encoded_size, &out, NULL, &diag) == QN_OK) {
        return fail("bad kind accepted");
    }

    memcpy(bad, encoded, encoded_size);
    bad[QN_QBC_V10_HEADER_SIZE + 5u] = 1u;
    if (qn_qbc_v10_data_decode(bad, encoded_size, &out, NULL, &diag) == QN_OK) {
        return fail("noncanonical name padding accepted");
    }

    memcpy(bad, encoded, encoded_size);
    uint32_t pool_offset = g32(bad + 116u);
    bad[pool_offset] = 0u;
    if (qn_qbc_v10_data_decode(bad, encoded_size, &out, NULL, &diag) == QN_OK) {
        return fail("string NUL accepted by decoder");
    }

    printf("QBIT_NOVA_V10_QBC_LAYOUT_STEP2B=PASS\n");
    printf("QBC_V10_VERSION_10=PASS\n");
    printf("QBC_V10_HEADER_128=PASS\n");
    printf("QBC_V10_VALUE_RECORD_80=PASS\n");
    printf("QBC_V10_CONSTANT_POOL=PASS\n");
    printf("QBC_V10_ROUND_TRIP=PASS\n");
    printf("QBC_V10_F32_NEGATIVE_ZERO_CANONICAL=PASS\n");
    printf("QBC_V10_STRING_NUL_REJECTED=PASS\n");
    printf("QBC_V10_FAIL_CLOSED=PASS\n");
    printf("QBC_V10_HI_BRO_SMILE=PASS\n");

    free(bad);
    free(encoded);
    free(q.constant_bytes);
    return 0;
}
