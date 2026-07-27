#include "qn.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static void qn_diag_vset(QNDiagnostic *diag, const char *code,
                         int line, int column, const char *fmt, va_list ap) {
    if (!diag) return;
    diag->line = line;
    diag->column = column;
    snprintf(diag->code, sizeof(diag->code), "%s",
             code && code[0] ? code : "QN-E-GENERIC");
    vsnprintf(diag->message, sizeof(diag->message), fmt, ap);
}

void qn_diag_set(QNDiagnostic *diag, int line, int column, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    qn_diag_vset(diag, "QN-E-GENERIC", line, column, fmt, ap);
    va_end(ap);
}

void qn_diag_set_code(QNDiagnostic *diag, const char *code,
                      int line, int column, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    qn_diag_vset(diag, code, line, column, fmt, ap);
    va_end(ap);
}

char *qn_read_text_file(const char *path, size_t *size_out, QNDiagnostic *diag) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        qn_diag_set(diag, 0, 0, "cannot open '%s': %s", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        qn_diag_set(diag, 0, 0, "cannot seek '%s'", path);
        fclose(f);
        return NULL;
    }
    long end = ftell(f);
    if (end < 0 || (uint64_t)end > QN_MAX_SOURCE_BYTES) {
        qn_diag_set(diag, 0, 0, "source exceeds %u bytes", QN_MAX_SOURCE_BYTES);
        fclose(f);
        return NULL;
    }
    rewind(f);
    size_t size = (size_t)end;
    char *buf = (char *)calloc(size + 1, 1);
    if (!buf) {
        qn_diag_set(diag, 0, 0, "out of memory reading '%s'", path);
        fclose(f);
        return NULL;
    }
    if (size && fread(buf, 1, size, f) != size) {
        qn_diag_set(diag, 0, 0, "short read from '%s'", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (size_out) *size_out = size;
    return buf;
}

bool qn_write_binary_file(const char *path, const uint8_t *data, size_t size, QNDiagnostic *diag) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        qn_diag_set(diag, 0, 0, "cannot create '%s': %s", path, strerror(errno));
        return false;
    }
    bool ok = fwrite(data, 1, size, f) == size;
    if (!ok) qn_diag_set(diag, 0, 0, "short write to '%s'", path);
    if (fclose(f) != 0) ok = false;
    return ok;
}

uint8_t *qn_read_binary_file(const char *path, size_t *size_out, QNDiagnostic *diag) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        qn_diag_set(diag, 0, 0, "cannot open '%s': %s", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        qn_diag_set(diag, 0, 0, "cannot seek '%s'", path);
        fclose(f);
        return NULL;
    }
    long end = ftell(f);
    if (end < 0 || (uint64_t)end > (64u * 1024u * 1024u)) {
        qn_diag_set(diag, 0, 0, "binary file too large");
        fclose(f);
        return NULL;
    }
    rewind(f);
    size_t size = (size_t)end;
    uint8_t *buf = (uint8_t *)malloc(size ? size : 1);
    if (!buf) {
        qn_diag_set(diag, 0, 0, "out of memory");
        fclose(f);
        return NULL;
    }
    if (size && fread(buf, 1, size, f) != size) {
        qn_diag_set(diag, 0, 0, "short read from '%s'", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (size_out) *size_out = size;
    return buf;
}

void qn_hex32(const uint8_t digest[32], char out[65]) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[64] = '\0';
}
