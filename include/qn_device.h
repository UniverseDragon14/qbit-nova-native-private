#ifndef QN_DEVICE_H
#define QN_DEVICE_H

#include "qn_qbc.h"

#define QN_DEVICE_BACKEND_NAME_CAP 16u
#define QN_DEVICE_CHIP_PATH_CAP 128u

typedef enum {
    QN_DEVICE_BACKEND_DENY = 0,
    QN_DEVICE_BACKEND_MOCK,
    QN_DEVICE_BACKEND_LINUX_GPIO
} QNDeviceBackend;

typedef struct {
    QNDeviceBackend backend;
    const char *gpiochip_path;
    uint32_t hold_ms;
} QNDeviceOptions;

typedef struct {
    char backend[QN_DEVICE_BACKEND_NAME_CAP];
    char gpiochip[QN_DEVICE_CHIP_PATH_CAP];
    uint32_t line_offset;
    uint32_t hold_ms;
    bool value_high;
    bool write_executed;
    bool reset_low;
    bool mock;
} QNDeviceResult;

bool qn_device_backend_parse(const char *text, QNDeviceBackend *out);
const char *qn_device_backend_name(QNDeviceBackend backend);
void qn_device_options_safe(QNDeviceOptions *options);
QNStatus qn_device_options_validate(const QNDeviceOptions *options,
                                    bool execution_required,
                                    QNDiagnostic *diag);

QNStatus qn_device_execute_gpio(const QNBytecode *bc,
                                const QNDeviceOptions *options,
                                QNDeviceResult *out,
                                QNDiagnostic *diag);

#endif
