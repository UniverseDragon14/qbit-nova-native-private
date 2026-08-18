#include "qn_qbc_v10_data.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void diag_set(QNDiagnostic *diag, const char *code, const char *message) {
    if (!diag) return;
    memset(diag, 0, sizeof(*diag));
    snprintf(diag->code, sizeof(diag->code), "%s", code);
    snprintf(diag->message, sizeof(diag->message), "%s", message);
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v) {
    for (unsigned i = 0; i < 8u; ++i) p[i] = (uint8_t)(v >> (8u * i));
}

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t get64(const uint8_t *p) {
    uint64_t v = 0u;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

static uint32_t canonical_f32_bits(uint32_t bits) {
    return bits == UINT32_C(0x80000000) ? 0u : bits;
}

static size_t bounded_name_length(const char name[QN_NAME_CAP]) {
    const char *nul = (const char *)memchr(name, '\0', QN_NAME_CAP);
    return nul ? (size_t)(nul - name) : QN_NAME_CAP;
}

static bool valid_name(const char name[QN_NAME_CAP]) {
    if (!name || name[0] == '\0') return false;
    return bounded_name_length(name) < QN_NAME_CAP;
}

static QNStatus validate_qir(const QNV10DataQIRProgram *qir,
                             QNDiagnostic *diag) {
    if (!qir || qir->abi_version != QN_V10_DATA_ABI_V1 ||
        !qir->requires_qbc_v10 || qir->value_count == 0u ||
        qir->value_count > QN_V10_MAX_DECLS ||
        qir->constant_bytes_size > QN_V10_MAX_CONSTANT_POOL_BYTES ||
        (qir->constant_bytes_size > 0u && !qir->constant_bytes)) {
        diag_set(diag, "QN-E7820", "invalid V10 data QIR contract");
        return QN_ERR_QBC;
    }

    uint32_t expected_pool_offset = 0u;
    for (uint16_t i = 0u; i < qir->value_count; ++i) {
        const QNV10DataQIRValue *value = &qir->values[i];
        if (!valid_name(value->name)) {
            diag_set(diag, "QN-E7820", "invalid V10 value name");
            return QN_ERR_QBC;
        }
        for (uint16_t j = 0u; j < i; ++j) {
            if (strncmp(value->name, qir->values[j].name, QN_NAME_CAP) == 0) {
                diag_set(diag, "QN-E7820", "duplicate V10 value name");
                return QN_ERR_QBC;
            }
        }

        if (value->kind == QN_VALUE_F32) {
            if (value->constant_offset != UINT32_MAX || value->byte_length != 4u) {
                diag_set(diag, "QN-E7820", "invalid canonical f32 record");
                return QN_ERR_QBC;
            }
            float f;
            memcpy(&f, &value->f32_bits, sizeof(f));
            if (!isfinite(f)) {
                diag_set(diag, "QN-E7820", "non-finite f32 record");
                return QN_ERR_QBC;
            }
            continue;
        }

        if (value->kind != QN_VALUE_STRING && value->kind != QN_VALUE_BYTES) {
            diag_set(diag, "QN-E7820", "unsupported V10 QBC data kind");
            return QN_ERR_QBC;
        }
        if (value->f32_bits != 0u || value->constant_offset != expected_pool_offset ||
            value->byte_length > qir->constant_bytes_size - expected_pool_offset) {
            diag_set(diag, "QN-E7820", "non-canonical V10 constant-pool range");
            return QN_ERR_QBC;
        }

        const uint8_t *bytes = qir->constant_bytes + value->constant_offset;
        QNStatus status;
        if (value->kind == QN_VALUE_STRING) {
            QNStringView view = {(const char *)bytes, value->byte_length};
            status = qn_string_validate(view, QN_MAX_STRING_BYTES, true, diag);
        } else {
            QNBytesView view = {bytes, value->byte_length};
            status = qn_bytes_validate(view, QN_MAX_BYTES_BUFFER, diag);
        }
        if (status != QN_OK) return QN_ERR_QBC;
        expected_pool_offset += value->byte_length;
    }

    if (expected_pool_offset != qir->constant_bytes_size) {
        diag_set(diag, "QN-E7820", "V10 constant pool has unreferenced bytes");
        return QN_ERR_QBC;
    }
    return QN_OK;
}

QNStatus qn_qbc_v10_data_encode(const QNV10DataQIRProgram *qir,
                                const uint8_t source_digest[32],
                                uint8_t **data_out,
                                size_t *size_out,
                                QNDiagnostic *diag) {
    if (!data_out || !size_out || !source_digest) {
        diag_set(diag, "QN-E7820", "invalid V10 encoder arguments");
        return QN_ERR_QBC;
    }
    *data_out = NULL;
    *size_out = 0u;
    if (validate_qir(qir, diag) != QN_OK) return QN_ERR_QBC;

    uint64_t table_bytes = (uint64_t)qir->value_count * QN_QBC_V10_VALUE_RECORD_SIZE;
    uint64_t pool_offset64 = (uint64_t)QN_QBC_V10_HEADER_SIZE + table_bytes;
    uint64_t total64 = pool_offset64 + qir->constant_bytes_size;
    if (pool_offset64 > UINT32_MAX || total64 > UINT32_MAX || total64 > SIZE_MAX) {
        diag_set(diag, "QN-E7821", "V10 QBC size overflow");
        return QN_ERR_QBC;
    }

    size_t total = (size_t)total64;
    uint8_t *data = (uint8_t *)calloc(total, 1u);
    if (!data) {
        diag_set(diag, "QN-E7821", "out of memory encoding V10 QBC");
        return QN_ERR_QBC;
    }

    memcpy(data, "QBCN", 4u);
    put16(data + 4u, QN_QBC_V10_VERSION);
    put16(data + 6u, QN_QBC_V10_HEADER_SIZE);
    put32(data + 8u, 0u);
    put16(data + 12u, 0u);
    put16(data + 14u, 0u);
    put64(data + 16u, 0u);
    put32(data + 24u, 1u);
    put64(data + 28u, 1u);
    memcpy(data + 36u, source_digest, 32u);
    put64(data + 68u, 0u);
    put16(data + 76u, 0u);
    put16(data + 78u, 0u);
    put64(data + 80u, 0u);
    put16(data + 88u, 0u);
    put16(data + 90u, 0u);
    put32(data + 92u, 0u);
    put16(data + 96u, 0u);
    put16(data + 98u, 0u);
    put16(data + 100u, 0u);
    put16(data + 102u, 0u);
    put16(data + 104u, qir->value_count);
    put16(data + 106u, QN_QBC_V10_VALUE_RECORD_SIZE);
    put32(data + 108u, qir->constant_bytes_size);
    put32(data + 112u, QN_QBC_V10_HEADER_SIZE);
    put32(data + 116u, (uint32_t)pool_offset64);
    put16(data + 120u, QN_V10_DATA_ABI_V1);
    put16(data + 122u, QN_QBC_V10_FLAG_DATA_ONLY);
    put32(data + 124u, 0u);

    size_t at = QN_QBC_V10_HEADER_SIZE;
    for (uint16_t i = 0u; i < qir->value_count; ++i) {
        const QNV10DataQIRValue *value = &qir->values[i];
        size_t name_len = bounded_name_length(value->name);
        memcpy(data + at, value->name, name_len);
        data[at + 64u] = (uint8_t)value->kind;
        data[at + 65u] = 0u;
        put16(data + at + 66u, 0u);
        put32(data + at + 68u, value->constant_offset);
        put32(data + at + 72u, value->byte_length);
        put32(data + at + 76u,
              value->kind == QN_VALUE_F32
                  ? canonical_f32_bits(value->f32_bits)
                  : value->f32_bits);
        at += QN_QBC_V10_VALUE_RECORD_SIZE;
    }

    if (qir->constant_bytes_size > 0u) {
        memcpy(data + at, qir->constant_bytes, qir->constant_bytes_size);
    }

    *data_out = data;
    *size_out = total;
    return QN_OK;
}

QNStatus qn_qbc_v10_data_decode(const uint8_t *data,
                                size_t size,
                                QNV10DataQIRProgram *out,
                                uint8_t source_digest_out[32],
                                QNDiagnostic *diag) {
    if (!data || !out || size < QN_QBC_V10_HEADER_SIZE) {
        diag_set(diag, "QN-E7822", "truncated or null V10 QBC");
        return QN_ERR_QBC;
    }
    memset(out, 0, sizeof(*out));
    if (memcmp(data, "QBCN", 4u) != 0 ||
        get16(data + 4u) != QN_QBC_V10_VERSION ||
        get16(data + 6u) != QN_QBC_V10_HEADER_SIZE) {
        diag_set(diag, "QN-E7822", "unsupported V10 QBC magic/version/header");
        return QN_ERR_QBC;
    }

    uint16_t value_count = get16(data + 104u);
    uint16_t record_size = get16(data + 106u);
    uint32_t pool_size = get32(data + 108u);
    uint32_t table_offset = get32(data + 112u);
    uint32_t pool_offset = get32(data + 116u);
    uint16_t data_abi = get16(data + 120u);
    uint16_t flags = get16(data + 122u);

    uint64_t expected_pool_offset = (uint64_t)QN_QBC_V10_HEADER_SIZE +
        (uint64_t)value_count * QN_QBC_V10_VALUE_RECORD_SIZE;
    uint64_t expected_size = expected_pool_offset + pool_size;

    if (get32(data + 8u) != 0u || get16(data + 12u) != 0u ||
        get16(data + 14u) != 0u || get64(data + 16u) != 0u ||
        get32(data + 24u) != 1u || get64(data + 28u) != 1u ||
        get64(data + 68u) != 0u || get16(data + 76u) != 0u ||
        get64(data + 80u) != 0u || get16(data + 88u) != 0u ||
        get16(data + 90u) != 0u || get32(data + 92u) != 0u ||
        get16(data + 96u) != 0u || get16(data + 98u) != 0u ||
        get16(data + 100u) != 0u || get16(data + 102u) != 0u ||
        value_count == 0u || value_count > QN_V10_MAX_DECLS ||
        record_size != QN_QBC_V10_VALUE_RECORD_SIZE ||
        pool_size > QN_V10_MAX_CONSTANT_POOL_BYTES ||
        table_offset != QN_QBC_V10_HEADER_SIZE ||
        expected_pool_offset > UINT32_MAX || pool_offset != (uint32_t)expected_pool_offset ||
        data_abi != QN_V10_DATA_ABI_V1 || flags != QN_QBC_V10_FLAG_DATA_ONLY ||
        get32(data + 124u) != 0u || expected_size != size) {
        diag_set(diag, "QN-E7823", "non-canonical V10 QBC header or section layout");
        return QN_ERR_QBC;
    }

    out->abi_version = QN_V10_DATA_ABI_V1;
    out->value_count = value_count;
    out->constant_bytes_size = pool_size;
    out->requires_qbc_v10 = true;

    if (pool_size > 0u) {
        out->constant_bytes = (uint8_t *)malloc(pool_size);
        if (!out->constant_bytes) {
            diag_set(diag, "QN-E7824", "out of memory decoding V10 constant pool");
            memset(out, 0, sizeof(*out));
            return QN_ERR_QBC;
        }
        memcpy(out->constant_bytes, data + pool_offset, pool_size);
    }

    size_t at = table_offset;
    uint32_t expected_value_pool_offset = 0u;
    for (uint16_t i = 0u; i < value_count; ++i) {
        QNV10DataQIRValue *value = &out->values[i];
        const void *nul = memchr(data + at, '\0', QN_NAME_CAP);
        if (!nul || data[at] == '\0' || data[at + 65u] != 0u ||
            get16(data + at + 66u) != 0u) {
            diag_set(diag, "QN-E7825", "invalid V10 value record name or flags");
            free(out->constant_bytes);
            memset(out, 0, sizeof(*out));
            return QN_ERR_QBC;
        }
        size_t name_len = (size_t)((const uint8_t *)nul - (data + at));
        for (size_t k = name_len + 1u; k < QN_NAME_CAP; ++k) {
            if (data[at + k] != 0u) {
                diag_set(diag, "QN-E7825", "non-canonical V10 value name padding");
                free(out->constant_bytes);
                memset(out, 0, sizeof(*out));
                return QN_ERR_QBC;
            }
        }
        memcpy(value->name, data + at, name_len);
        value->kind = (QNValueKind)data[at + 64u];
        value->constant_offset = get32(data + at + 68u);
        value->byte_length = get32(data + at + 72u);
        value->f32_bits = get32(data + at + 76u);

        for (uint16_t j = 0u; j < i; ++j) {
            if (strncmp(value->name, out->values[j].name, QN_NAME_CAP) == 0) {
                diag_set(diag, "QN-E7825", "duplicate V10 value name in QBC");
                free(out->constant_bytes);
                memset(out, 0, sizeof(*out));
                return QN_ERR_QBC;
            }
        }

        if (value->kind == QN_VALUE_F32) {
            if (value->constant_offset != UINT32_MAX || value->byte_length != 4u ||
                value->f32_bits == UINT32_C(0x80000000)) {
                diag_set(diag, "QN-E7825", "invalid canonical V10 f32 record");
                free(out->constant_bytes);
                memset(out, 0, sizeof(*out));
                return QN_ERR_QBC;
            }
            float f;
            memcpy(&f, &value->f32_bits, sizeof(f));
            if (!isfinite(f)) {
                diag_set(diag, "QN-E7825", "non-finite V10 f32 record");
                free(out->constant_bytes);
                memset(out, 0, sizeof(*out));
                return QN_ERR_QBC;
            }
        } else if (value->kind == QN_VALUE_STRING || value->kind == QN_VALUE_BYTES) {
            if (value->f32_bits != 0u || value->constant_offset != expected_value_pool_offset ||
                value->byte_length > pool_size - expected_value_pool_offset) {
                diag_set(diag, "QN-E7825", "invalid canonical V10 constant-pool record");
                free(out->constant_bytes);
                memset(out, 0, sizeof(*out));
                return QN_ERR_QBC;
            }
            const uint8_t *bytes = out->constant_bytes + value->constant_offset;
            QNStatus status;
            if (value->kind == QN_VALUE_STRING) {
                QNStringView view = {(const char *)bytes, value->byte_length};
                status = qn_string_validate(view, QN_MAX_STRING_BYTES, true, diag);
            } else {
                QNBytesView view = {bytes, value->byte_length};
                status = qn_bytes_validate(view, QN_MAX_BYTES_BUFFER, diag);
            }
            if (status != QN_OK) {
                free(out->constant_bytes);
                memset(out, 0, sizeof(*out));
                return QN_ERR_QBC;
            }
            expected_value_pool_offset += value->byte_length;
        } else {
            diag_set(diag, "QN-E7825", "unsupported V10 value kind in QBC");
            free(out->constant_bytes);
            memset(out, 0, sizeof(*out));
            return QN_ERR_QBC;
        }
        at += QN_QBC_V10_VALUE_RECORD_SIZE;
    }

    if (expected_value_pool_offset != pool_size) {
        diag_set(diag, "QN-E7825", "V10 constant pool has unreferenced bytes");
        free(out->constant_bytes);
        memset(out, 0, sizeof(*out));
        return QN_ERR_QBC;
    }

    if (source_digest_out) memcpy(source_digest_out, data + 36u, 32u);
    return QN_OK;
}
