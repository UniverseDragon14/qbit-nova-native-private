#define _POSIX_C_SOURCE 200809L

#include "qn_device.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef __linux__
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

#define QN_DEVICE_DEFAULT_HOLD_MS 250u

bool qn_device_backend_parse(const char *text, QNDeviceBackend *out) {
    if (!text || !out) return false;
    if (strcmp(text, "deny") == 0) *out = QN_DEVICE_BACKEND_DENY;
    else if (strcmp(text, "mock") == 0) *out = QN_DEVICE_BACKEND_MOCK;
    else if (strcmp(text, "linux-gpio") == 0)
        *out = QN_DEVICE_BACKEND_LINUX_GPIO;
    else return false;
    return true;
}

const char *qn_device_backend_name(QNDeviceBackend backend) {
    switch (backend) {
        case QN_DEVICE_BACKEND_DENY: return "deny";
        case QN_DEVICE_BACKEND_MOCK: return "mock";
        case QN_DEVICE_BACKEND_LINUX_GPIO: return "linux-gpio";
        default: return "invalid";
    }
}

void qn_device_options_safe(QNDeviceOptions *options) {
    if (!options) return;
    memset(options, 0, sizeof(*options));
    options->backend = QN_DEVICE_BACKEND_DENY;
    options->hold_ms = QN_DEVICE_DEFAULT_HOLD_MS;
}

static bool qn_gpiochip_path_valid(const char *path) {
    static const char prefix[] = "/dev/gpiochip";
    if (!path || strlen(path) >= QN_DEVICE_CHIP_PATH_CAP ||
        strncmp(path, prefix, sizeof(prefix) - 1u) != 0) return false;
    const char *suffix = path + sizeof(prefix) - 1u;
    if (!*suffix) return false;
    for (; *suffix; ++suffix) {
        if (!isdigit((unsigned char)*suffix)) return false;
    }
    return true;
}

QNStatus qn_device_options_validate(const QNDeviceOptions *options,
                                    bool execution_required,
                                    QNDiagnostic *diag) {
    if (!options || !diag) return QN_ERR_PARSE;
    if (options->hold_ms > QN_MAX_DEVICE_HOLD_MS) {
        qn_diag_set_code(diag, "QN-E7812", 0, 0,
                         "device hold must be 0..%u ms",
                         QN_MAX_DEVICE_HOLD_MS);
        return QN_ERR_LIMIT;
    }
    if (execution_required && options->backend == QN_DEVICE_BACKEND_DENY) {
        qn_diag_set_code(diag, "QN-E7811", 0, 0,
                         "device backend is deny; select mock or linux-gpio explicitly");
        return QN_ERR_RUNTIME;
    }
    if (options->backend == QN_DEVICE_BACKEND_LINUX_GPIO) {
        if (!qn_gpiochip_path_valid(options->gpiochip_path)) {
            qn_diag_set_code(diag, "QN-E7812", 0, 0,
                             "linux-gpio requires an explicit /dev/gpiochipN path");
            return QN_ERR_PARSE;
        }
    } else if (options->gpiochip_path != NULL) {
        qn_diag_set_code(diag, "QN-E7812", 0, 0,
                         "--gpiochip is valid only with linux-gpio backend");
        return QN_ERR_PARSE;
    }
    return QN_OK;
}

#ifdef __linux__
static QNStatus qn_linux_gpio_execute(const char *chip_path,
                                      uint32_t line_offset,
                                      bool high,
                                      uint32_t hold_ms,
                                      QNDeviceResult *out,
                                      QNDiagnostic *diag) {
    int chip_fd = open(chip_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (chip_fd < 0) {
        qn_diag_set_code(diag, "QN-E7813", 0, 0,
                         "cannot open GPIO chip '%s': %s",
                         chip_path, strerror(errno));
        return QN_ERR_IO;
    }

    struct stat chip_stat;
    int stat_status = fstat(chip_fd, &chip_stat);
    int stat_error = stat_status != 0 ? errno : 0;
    if (stat_status != 0 || !S_ISCHR(chip_stat.st_mode)) {
        close(chip_fd);
        qn_diag_set_code(diag, "QN-E7813", 0, 0,
                         "GPIO chip path is not a character device: %s%s%s",
                         chip_path, stat_error ? ": " : "",
                         stat_error ? strerror(stat_error) : "");
        return QN_ERR_IO;
    }

    struct gpio_v2_line_request request;
    memset(&request, 0, sizeof(request));
    request.offsets[0] = line_offset;
    request.num_lines = 1u;
    request.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(request.consumer, sizeof(request.consumer),
             "qbit-nova-stage8");

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request) < 0) {
        int saved = errno;
        close(chip_fd);
        qn_diag_set_code(diag, "QN-E7814", 0, 0,
                         "cannot request GPIO line %u on '%s': %s",
                         line_offset, chip_path, strerror(saved));
        return QN_ERR_RUNTIME;
    }
    close(chip_fd);

    struct gpio_v2_line_values values;
    memset(&values, 0, sizeof(values));
    values.mask = UINT64_C(1);
    values.bits = high ? UINT64_C(1) : UINT64_C(0);

    if (ioctl(request.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0) {
        int saved = errno;
        close(request.fd);
        qn_diag_set_code(diag, "QN-E7815", 0, 0,
                         "GPIO line %u write failed: %s",
                         line_offset, strerror(saved));
        return QN_ERR_RUNTIME;
    }

    out->write_executed = true;

    int sleep_error = 0;
    if (high && hold_ms > 0u) {
        struct timespec remaining = {
            .tv_sec = (time_t)(hold_ms / 1000u),
            .tv_nsec = (long)(hold_ms % 1000u) * 1000000L
        };
        while (nanosleep(&remaining, &remaining) < 0) {
            if (errno != EINTR) {
                sleep_error = errno;
                break;
            }
        }
    }

    values.bits = UINT64_C(0);
    if (ioctl(request.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0) {
        int saved = errno;
        close(request.fd);
        qn_diag_set_code(diag, "QN-E7816", 0, 0,
                         "GPIO line %u safety reset failed: %s",
                         line_offset, strerror(saved));
        return QN_ERR_RUNTIME;
    }
    out->reset_low = true;
    close(request.fd);
    if (sleep_error != 0) {
        qn_diag_set_code(diag, "QN-E7815", 0, 0,
                         "GPIO hold timer failed: %s",
                         strerror(sleep_error));
        return QN_ERR_RUNTIME;
    }
    return QN_OK;
}
#endif

QNStatus qn_device_execute_gpio(const QNBytecode *bc,
                                const QNDeviceOptions *options,
                                QNDeviceResult *out,
                                QNDiagnostic *diag) {
    if (!bc || !options || !out || !diag ||
        !qn_qbc_is_gpio_output_program(bc)) {
        if (diag) qn_diag_set_code(diag, "QN-E7810", 0, 0,
                                   "invalid bounded GPIO execution contract");
        return QN_ERR_RUNTIME;
    }

    memset(out, 0, sizeof(*out));
    uint32_t hold_ms = options->hold_ms;
    QNStatus option_status = qn_device_options_validate(options, true, diag);
    if (option_status != QN_OK) return option_status;

    out->line_offset = bc->instructions[0].imm;
    out->value_high = bc->instructions[1].b != 0u;
    out->hold_ms = hold_ms;
    snprintf(out->backend, sizeof(out->backend), "%s",
             qn_device_backend_name(options->backend));

    if (options->backend == QN_DEVICE_BACKEND_MOCK) {
        snprintf(out->gpiochip, sizeof(out->gpiochip), "mock");
        out->mock = true;
        out->write_executed = true;
        out->reset_low = true;
        return QN_OK;
    }

    if (options->backend != QN_DEVICE_BACKEND_LINUX_GPIO) {
        qn_diag_set_code(diag, "QN-E7812", 0, 0,
                         "invalid device backend");
        return QN_ERR_PARSE;
    }
    snprintf(out->gpiochip, sizeof(out->gpiochip), "%s",
             options->gpiochip_path);

#ifdef __linux__
    return qn_linux_gpio_execute(options->gpiochip_path,
                                 out->line_offset, out->value_high,
                                 hold_ms, out, diag);
#else
    qn_diag_set_code(diag, "QN-E7817", 0, 0,
                     "linux-gpio backend is unavailable on this platform");
    return QN_ERR_RUNTIME;
#endif
}
