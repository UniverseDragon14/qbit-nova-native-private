#ifndef QN_QBC_V10_DATA_H
#define QN_QBC_V10_DATA_H

#include "qn_v10_data.h"

enum {
    QN_QBC_V10_VERSION = 10u,
    QN_QBC_V10_HEADER_SIZE = 128u,
    QN_QBC_V10_VALUE_RECORD_SIZE = 80u,
    QN_QBC_V10_FLAG_DATA_ONLY = 1u
};

QNStatus qn_qbc_v10_data_encode(const QNV10DataQIRProgram *qir,
                                const uint8_t source_digest[32],
                                uint8_t **data_out,
                                size_t *size_out,
                                QNDiagnostic *diag);

QNStatus qn_qbc_v10_data_decode(const uint8_t *data,
                                size_t size,
                                QNV10DataQIRProgram *out,
                                uint8_t source_digest_out[32],
                                QNDiagnostic *diag);

#endif
