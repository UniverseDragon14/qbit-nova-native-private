#include "qn.h"
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

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

bool qn_write_binary_file_atomic(
    const char *path,
    const uint8_t *data,
    size_t size,
    QNDiagnostic *diag
) {
#if defined(__unix__) || defined(__APPLE__)
    if (!path ||
        path[0] == '\0' ||
        (!data && size != 0u)) {
        qn_diag_set(
            diag,
            0,
            0,
            "invalid atomic binary output arguments"
        );
        return false;
    }

    const size_t suffix_capacity = 64u;
    const size_t path_length = strlen(path);

    if (path_length > ((size_t)-1) - suffix_capacity) {
        qn_diag_set(
            diag,
            0,
            0,
            "atomic output path is too long"
        );
        return false;
    }

    const size_t temp_capacity =
        path_length + suffix_capacity;

    char *temp_path =
        (char *)malloc(temp_capacity);

    if (!temp_path) {
        qn_diag_set(
            diag,
            0,
            0,
            "out of memory creating atomic output path"
        );
        return false;
    }

    int fd = -1;

    /*
     * STEP9_QBC_ATOMIC_PUBLISHER
     *
     * Temporary output lives beside the destination.
     * O_CREAT | O_EXCL prevents following or replacing an
     * existing temporary path.
     */
    for (unsigned attempt = 0u;
         attempt < 128u;
         ++attempt) {
        int formatted = snprintf(
            temp_path,
            temp_capacity,
            "%s.tmp.%ld.%u",
            path,
            (long)getpid(),
            attempt
        );

        if (formatted < 0 ||
            (size_t)formatted >= temp_capacity) {
            qn_diag_set(
                diag,
                0,
                0,
                "cannot construct atomic output path"
            );
            free(temp_path);
            return false;
        }

        fd = open(
            temp_path,
            O_WRONLY | O_CREAT | O_EXCL,
            0666
        );

        if (fd >= 0) {
            break;
        }

        if (errno != EEXIST) {
            qn_diag_set(
                diag,
                0,
                0,
                "cannot create temporary output for '%s': %s",
                path,
                strerror(errno)
            );
            free(temp_path);
            return false;
        }
    }

    if (fd < 0) {
        qn_diag_set(
            diag,
            0,
            0,
            "cannot create unique temporary output for '%s'",
            path
        );
        free(temp_path);
        return false;
    }

    bool ok = true;
    size_t offset = 0u;

    while (offset < size) {
        size_t chunk = size - offset;

        if (chunk > 1024u * 1024u) {
            chunk = 1024u * 1024u;
        }

        ssize_t written = write(
            fd,
            data + offset,
            chunk
        );

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            qn_diag_set(
                diag,
                0,
                0,
                "cannot write temporary output for '%s': %s",
                path,
                strerror(errno)
            );
            ok = false;
            break;
        }

        if (written == 0) {
            qn_diag_set(
                diag,
                0,
                0,
                "short write to temporary output for '%s'",
                path
            );
            ok = false;
            break;
        }

        offset += (size_t)written;
    }

    if (ok && fsync(fd) != 0) {
        qn_diag_set(
            diag,
            0,
            0,
            "cannot sync temporary output for '%s': %s",
            path,
            strerror(errno)
        );
        ok = false;
    }

    if (close(fd) != 0) {
        if (ok) {
            qn_diag_set(
                diag,
                0,
                0,
                "cannot finalize temporary output for '%s': %s",
                path,
                strerror(errno)
            );
        }
        ok = false;
    }

    if (!ok) {
        (void)unlink(temp_path);
        free(temp_path);
        return false;
    }

    /*
     * The destination is untouched until this point.
     *
     * rename() publishes the fully written sibling file
     * atomically within the same filesystem namespace.
     *
     * If destination is a symlink, rename replaces the symlink
     * entry itself instead of opening its target.
     */
    if (rename(temp_path, path) != 0) {
        int publish_errno = errno;

        qn_diag_set(
            diag,
            0,
            0,
            "cannot atomically publish '%s': %s",
            path,
            strerror(publish_errno)
        );

        (void)unlink(temp_path);
        free(temp_path);
        return false;
    }

    free(temp_path);
    return true;
#else
    (void)path;
    (void)data;
    (void)size;

    qn_diag_set(
        diag,
        0,
        0,
        "atomic binary publishing is unsupported "
        "on this platform"
    );

    return false;
#endif
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
