#define _POSIX_C_SOURCE 200809L

#include "qn_replay_ledger.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define QNRL_HEADER "QNRL1\n"

typedef struct {
    int directory_fd;
    char name[QN_PATH_CAP];
} QNReplayPath;

static bool digest_is_zero(
    const uint8_t digest[QN_REPLAY_LEDGER_DIGEST_BYTES]
) {
    uint8_t combined = 0u;

    for (size_t index = 0u;
         index < QN_REPLAY_LEDGER_DIGEST_BYTES;
         ++index) {
        combined |= digest[index];
    }

    return combined == 0u;
}

static size_t bounded_path_length(const char *path) {
    if (!path) return 0u;

    size_t length = 0u;

    while (length < QN_PATH_CAP &&
           path[length] != '\0') {
        ++length;
    }

    return length;
}

static bool encode_digest(
    const uint8_t digest[QN_REPLAY_LEDGER_DIGEST_BYTES],
    uint8_t out[QN_REPLAY_LEDGER_RECORD_BYTES]
) {
    static const uint8_t hex[] =
        "0123456789abcdef";

    if (!digest || !out) return false;

    for (size_t index = 0u;
         index < QN_REPLAY_LEDGER_DIGEST_BYTES;
         ++index) {
        out[index * 2u] =
            hex[digest[index] >> 4];

        out[index * 2u + 1u] =
            hex[digest[index] & 0x0fu];
    }

    out[64] = (uint8_t)'\n';
    return true;
}

static bool is_lower_hex(uint8_t value) {
    return
        (value >= (uint8_t)'0' &&
         value <= (uint8_t)'9') ||
        (value >= (uint8_t)'a' &&
         value <= (uint8_t)'f');
}

static int lock_fd(int fd, short type) {
    struct flock lock = {
        .l_type = type,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0,
        .l_pid = 0,
    };

    int result;

    do {
        result = fcntl(fd, F_SETLKW, &lock);
    } while (result < 0 && errno == EINTR);

    return result;
}

static void unlock_fd(int fd) {
    struct flock lock = {
        .l_type = F_UNLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0,
        .l_pid = 0,
    };

    while (fcntl(fd, F_SETLK, &lock) < 0 &&
           errno == EINTR) {
    }
}

static bool pread_exact(
    int fd,
    uint8_t *data,
    size_t size,
    off_t offset
) {
    size_t done = 0u;

    while (done < size) {
        ssize_t amount = pread(
            fd,
            data + done,
            size - done,
            offset + (off_t)done
        );

        if (amount < 0 && errno == EINTR) {
            continue;
        }

        if (amount <= 0) {
            return false;
        }

        done += (size_t)amount;
    }

    return true;
}

static bool pwrite_exact(
    int fd,
    const uint8_t *data,
    size_t size,
    off_t offset
) {
    size_t done = 0u;

    while (done < size) {
        ssize_t amount = pwrite(
            fd,
            data + done,
            size - done,
            offset + (off_t)done
        );

        if (amount < 0 && errno == EINTR) {
            continue;
        }

        if (amount <= 0) {
            return false;
        }

        done += (size_t)amount;
    }

    return true;
}

static void replay_path_close(QNReplayPath *path) {
    if (path && path->directory_fd >= 0) {
        (void)close(path->directory_fd);
        path->directory_fd = -1;
    }
}

static QNStatus replay_path_open(
    const char *path,
    QNReplayPath *out,
    QNDiagnostic *diag
) {
    if (!out) {
        qn_diag_set_code(
            diag,
            "QN-E5201",
            0,
            0,
            "invalid replay ledger path output"
        );
        return QN_ERR_RUNTIME;
    }

    out->directory_fd = -1;
    out->name[0] = '\0';

    size_t length = bounded_path_length(path);

    if (length == 0u || length >= QN_PATH_CAP) {
        qn_diag_set_code(
            diag,
            "QN-E5201",
            0,
            0,
            "invalid replay ledger path"
        );
        return QN_ERR_RUNTIME;
    }

    char copy[QN_PATH_CAP];
    memcpy(copy, path, length + 1u);

    char *slash = strrchr(copy, '/');
    const char *directory = ".";
    const char *name = copy;

    if (slash) {
        *slash = '\0';
        name = slash + 1;

        if (slash == copy) {
            directory = "/";
        } else {
            directory = copy;
        }
    }

    if (name[0] == '\0' ||
        strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
        qn_diag_set_code(
            diag,
            "QN-E5201",
            0,
            0,
            "replay ledger path has an invalid file name"
        );
        return QN_ERR_RUNTIME;
    }

    size_t name_length = strlen(name);

    if (name_length >= sizeof(out->name)) {
        qn_diag_set_code(
            diag,
            "QN-E5201",
            0,
            0,
            "replay ledger file name is oversized"
        );
        return QN_ERR_LIMIT;
    }

    int directory_fd;

    do {
        directory_fd = open(
            directory,
            O_RDONLY |
                O_DIRECTORY |
                O_CLOEXEC |
                O_NOFOLLOW
        );
    } while (directory_fd < 0 && errno == EINTR);

    if (directory_fd < 0) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "cannot open replay ledger directory '%s': %s",
            directory,
            strerror(errno)
        );
        return QN_ERR_IO;
    }

    memcpy(out->name, name, name_length + 1u);
    out->directory_fd = directory_fd;
    return QN_OK;
}

static QNStatus validate_fd(
    int fd,
    bool allow_empty,
    off_t *size_out,
    size_t *record_count_out,
    const uint8_t target[QN_REPLAY_LEDGER_RECORD_BYTES],
    bool *seen,
    QNDiagnostic *diag
) {
    struct stat metadata;

    if (fstat(fd, &metadata) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "cannot inspect replay ledger: %s",
            strerror(errno)
        );
        return QN_ERR_IO;
    }

    if (!S_ISREG(metadata.st_mode) ||
        metadata.st_nlink != 1) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "replay ledger is not a private regular file"
        );
        return QN_ERR_IO;
    }

    if ((metadata.st_mode & 0077) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "replay ledger permissions expose group or other access"
        );
        return QN_ERR_IO;
    }

    if (metadata.st_size < 0 ||
        (uint64_t)metadata.st_size >
            (uint64_t)QN_REPLAY_LEDGER_MAX_FILE_BYTES) {
        qn_diag_set_code(
            diag,
            "QN-E5205",
            0,
            0,
            "replay ledger exceeds configured capacity"
        );
        return QN_ERR_LIMIT;
    }

    off_t size = metadata.st_size;

    *seen = false;
    *record_count_out = 0u;
    *size_out = size;

    if (size == 0) {
        if (allow_empty) {
            return QN_OK;
        }

        qn_diag_set_code(
            diag,
            "QN-E5203",
            1,
            1,
            "existing replay ledger is empty"
        );
        return QN_ERR_RUNTIME;
    }

    if (size < (off_t)QN_REPLAY_LEDGER_HEADER_BYTES ||
        ((size - (off_t)QN_REPLAY_LEDGER_HEADER_BYTES) %
         (off_t)QN_REPLAY_LEDGER_RECORD_BYTES) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5203",
            0,
            0,
            "replay ledger has a truncated or trailing record"
        );
        return QN_ERR_RUNTIME;
    }

    uint8_t header[QN_REPLAY_LEDGER_HEADER_BYTES];

    if (!pread_exact(fd, header, sizeof(header), 0) ||
        memcmp(header, QNRL_HEADER, sizeof(header)) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5203",
            1,
            1,
            "invalid replay ledger header; expected QNRL1"
        );
        return QN_ERR_RUNTIME;
    }

    size_t count = (size_t)(
        (size - (off_t)QN_REPLAY_LEDGER_HEADER_BYTES) /
        (off_t)QN_REPLAY_LEDGER_RECORD_BYTES
    );

    if (count > QN_REPLAY_LEDGER_MAX_RECORDS) {
        qn_diag_set_code(
            diag,
            "QN-E5205",
            0,
            0,
            "replay ledger record limit reached"
        );
        return QN_ERR_LIMIT;
    }

    uint8_t record[QN_REPLAY_LEDGER_RECORD_BYTES];

    for (size_t index = 0u; index < count; ++index) {
        off_t offset =
            (off_t)QN_REPLAY_LEDGER_HEADER_BYTES +
            (off_t)(index * QN_REPLAY_LEDGER_RECORD_BYTES);

        if (!pread_exact(
                fd,
                record,
                sizeof(record),
                offset)) {
            qn_diag_set_code(
                diag,
                "QN-E5202",
                0,
                0,
                "short read from replay ledger"
            );
            return QN_ERR_IO;
        }

        if (record[64] != (uint8_t)'\n') {
            qn_diag_set_code(
                diag,
                "QN-E5203",
                (int)index + 2,
                65,
                "replay ledger record must end with LF"
            );
            return QN_ERR_RUNTIME;
        }

        for (size_t column = 0u; column < 64u; ++column) {
            if (!is_lower_hex(record[column])) {
                qn_diag_set_code(
                    diag,
                    "QN-E5203",
                    (int)index + 2,
                    (int)column + 1,
                    "replay ledger digest is not lowercase hexadecimal"
                );
                return QN_ERR_RUNTIME;
            }
        }

        if (memcmp(record, target, 64u) == 0) {
            *seen = true;
        }
    }

    *record_count_out = count;
    return QN_OK;
}

static QNStatus open_for_consume(
    const QNReplayPath *path,
    int *fd_out,
    bool *created_out,
    QNDiagnostic *diag
) {
    enum { CREATE_VISIBILITY_RETRIES = 64 };
    for (int attempt = 0; attempt < CREATE_VISIBILITY_RETRIES; ++attempt) {
        int fd;

        do {
            fd = openat(
                path->directory_fd,
                path->name,
                O_RDWR |
                    O_CLOEXEC |
                    O_NOFOLLOW
            );
        } while (fd < 0 && errno == EINTR);

        if (fd >= 0) {
            /*
             * An O_EXCL creator publishes the directory entry before it can
             * acquire the file lock and initialize QNRL1. A concurrent opener
             * must not misclassify that short visibility window as a corrupt
             * pre-existing ledger. Never wait while holding the file lock;
             * close, yield briefly, and reopen so the creator can proceed.
             */
            struct stat metadata;
            if (fstat(fd, &metadata) != 0) {
                int saved_errno = errno;
                (void)close(fd);
                qn_diag_set_code(diag, "QN-E5202", 0, 0,
                                 "cannot inspect replay ledger '%s': %s",
                                 path->name, strerror(saved_errno));
                return QN_ERR_IO;
            }
            if (metadata.st_size == 0 &&
                attempt + 1 < CREATE_VISIBILITY_RETRIES) {
                (void)close(fd);
                struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000L};
                while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
                continue;
            }
            *fd_out = fd;
            *created_out = false;
            return QN_OK;
        }

        if (errno != ENOENT) {
            qn_diag_set_code(
                diag,
                "QN-E5202",
                0,
                0,
                "cannot open replay ledger '%s': %s",
                path->name,
                strerror(errno)
            );
            return QN_ERR_IO;
        }

        do {
            fd = openat(
                path->directory_fd,
                path->name,
                O_RDWR |
                    O_CREAT |
                    O_EXCL |
                    O_CLOEXEC |
                    O_NOFOLLOW,
                S_IRUSR | S_IWUSR
            );
        } while (fd < 0 && errno == EINTR);

        if (fd >= 0) {
            if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
                int saved_errno = errno;
                (void)close(fd);

                qn_diag_set_code(
                    diag,
                    "QN-E5202",
                    0,
                    0,
                    "cannot protect new replay ledger '%s': %s",
                    path->name,
                    strerror(saved_errno)
                );

                return QN_ERR_IO;
            }

            *fd_out = fd;
            *created_out = true;
            return QN_OK;
        }

        if (errno != EEXIST) {
            qn_diag_set_code(
                diag,
                "QN-E5202",
                0,
                0,
                "cannot create replay ledger '%s': %s",
                path->name,
                strerror(errno)
            );
            return QN_ERR_IO;
        }
    }

    qn_diag_set_code(
        diag,
        "QN-E5202",
        0,
        0,
        "replay ledger path changed repeatedly"
    );

    return QN_ERR_IO;
}

QNStatus qn_replay_ledger_contains(
    const char *path,
    const uint8_t token_digest[QN_REPLAY_LEDGER_DIGEST_BYTES],
    bool *seen,
    QNDiagnostic *diag
) {
    if (!path ||
        !token_digest ||
        !seen ||
        digest_is_zero(token_digest)) {
        qn_diag_set_code(
            diag,
            "QN-E5201",
            0,
            0,
            "invalid replay ledger lookup arguments"
        );
        return QN_ERR_RUNTIME;
    }

    *seen = false;

    uint8_t encoded[QN_REPLAY_LEDGER_RECORD_BYTES];
    (void)encode_digest(token_digest, encoded);

    QNReplayPath replay_path;
    QNStatus status = replay_path_open(
        path,
        &replay_path,
        diag
    );

    if (status != QN_OK) {
        return status;
    }

    int fd;

    do {
        fd = openat(
            replay_path.directory_fd,
            replay_path.name,
            O_RDONLY |
                O_CLOEXEC |
                O_NOFOLLOW
        );
    } while (fd < 0 && errno == EINTR);

    if (fd < 0 && errno == ENOENT) {
        replay_path_close(&replay_path);
        return QN_OK;
    }

    if (fd < 0) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "cannot open replay ledger '%s': %s",
            replay_path.name,
            strerror(errno)
        );
        replay_path_close(&replay_path);
        return QN_ERR_IO;
    }

    if (lock_fd(fd, F_RDLCK) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "cannot lock replay ledger '%s': %s",
            replay_path.name,
            strerror(errno)
        );
        (void)close(fd);
        replay_path_close(&replay_path);
        return QN_ERR_IO;
    }

    off_t size = 0;
    size_t count = 0u;

    status = validate_fd(
        fd,
        false,
        &size,
        &count,
        encoded,
        seen,
        diag
    );

    (void)size;
    (void)count;

    unlock_fd(fd);

    if (close(fd) != 0 && status == QN_OK) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "cannot close replay ledger '%s'",
            replay_path.name
        );
        status = QN_ERR_IO;
    }

    replay_path_close(&replay_path);
    return status;
}

QNStatus qn_replay_ledger_consume(
    const char *path,
    const uint8_t token_digest[QN_REPLAY_LEDGER_DIGEST_BYTES],
    bool *consumed,
    QNDiagnostic *diag
) {
    if (!path ||
        !token_digest ||
        !consumed ||
        digest_is_zero(token_digest)) {
        qn_diag_set_code(
            diag,
            "QN-E5201",
            0,
            0,
            "invalid replay ledger consume arguments"
        );
        return QN_ERR_RUNTIME;
    }

    *consumed = false;

    uint8_t encoded[QN_REPLAY_LEDGER_RECORD_BYTES];
    (void)encode_digest(token_digest, encoded);

    QNReplayPath replay_path;
    QNStatus status = replay_path_open(
        path,
        &replay_path,
        diag
    );

    if (status != QN_OK) {
        return status;
    }

    int fd = -1;
    bool created = false;

    status = open_for_consume(
        &replay_path,
        &fd,
        &created,
        diag
    );

    if (status != QN_OK) {
        replay_path_close(&replay_path);
        return status;
    }

    if (lock_fd(fd, F_WRLCK) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "cannot lock replay ledger '%s': %s",
            replay_path.name,
            strerror(errno)
        );
        (void)close(fd);
        replay_path_close(&replay_path);
        return QN_ERR_IO;
    }

    off_t size = 0;
    size_t count = 0u;
    bool seen = false;

    status = validate_fd(
        fd,
        created,
        &size,
        &count,
        encoded,
        &seen,
        diag
    );

    if (status == QN_OK && seen) {
        qn_diag_set_code(
            diag,
            "QN-E5204",
            0,
            0,
            "signed approval token was already consumed"
        );
        status = QN_ERR_RUNTIME;
    }

    if (status == QN_OK &&
        count >= QN_REPLAY_LEDGER_MAX_RECORDS) {
        qn_diag_set_code(
            diag,
            "QN-E5205",
            0,
            0,
            "replay ledger record limit reached"
        );
        status = QN_ERR_LIMIT;
    }

    off_t append_at = size;

    if (status == QN_OK && size == 0) {
        if (!pwrite_exact(
                fd,
                (const uint8_t *)QNRL_HEADER,
                QN_REPLAY_LEDGER_HEADER_BYTES,
                0)) {
            qn_diag_set_code(
                diag,
                "QN-E5202",
                0,
                0,
                "cannot initialize replay ledger '%s': %s",
                replay_path.name,
                strerror(errno)
            );
            status = QN_ERR_IO;
        } else {
            append_at =
                (off_t)QN_REPLAY_LEDGER_HEADER_BYTES;
        }
    }

    if (status == QN_OK &&
        !pwrite_exact(
            fd,
            encoded,
            sizeof(encoded),
            append_at)) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "cannot append replay ledger '%s': %s",
            replay_path.name,
            strerror(errno)
        );
        status = QN_ERR_IO;
    }

    if (status == QN_OK && fsync(fd) != 0) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "cannot persist replay ledger '%s': %s",
            replay_path.name,
            strerror(errno)
        );
        status = QN_ERR_IO;
    }

    if (created &&
        fsync(replay_path.directory_fd) != 0 &&
        status == QN_OK) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "cannot persist replay ledger directory: %s",
            strerror(errno)
        );
        status = QN_ERR_IO;
    }

    unlock_fd(fd);

    if (close(fd) != 0 && status == QN_OK) {
        qn_diag_set_code(
            diag,
            "QN-E5202",
            0,
            0,
            "cannot close replay ledger '%s'",
            replay_path.name
        );
        status = QN_ERR_IO;
    }

    replay_path_close(&replay_path);

    if (status == QN_OK) {
        *consumed = true;
    }

    return status;
}
