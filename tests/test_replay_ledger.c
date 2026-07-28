#define _POSIX_C_SOURCE 200809L

#include "qn_replay_ledger.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void make_digest(
    uint8_t digest[QN_REPLAY_LEDGER_DIGEST_BYTES],
    uint8_t marker
) {
    for (size_t index = 0u;
         index < QN_REPLAY_LEDGER_DIGEST_BYTES;
         ++index) {
        digest[index] = (uint8_t)(marker + index);
    }
}

static int write_all(
    int fd,
    const uint8_t *data,
    size_t size
) {
    size_t done = 0u;

    while (done < size) {
        ssize_t amount = write(
            fd,
            data + done,
            size - done
        );

        if (amount < 0 && errno == EINTR) {
            continue;
        }

        if (amount <= 0) {
            return 0;
        }

        done += (size_t)amount;
    }

    return 1;
}

static int write_bytes(
    const char *path,
    const uint8_t *data,
    size_t size,
    mode_t mode
) {
    int fd = open(
        path,
        O_WRONLY | O_CREAT | O_TRUNC,
        mode
    );

    if (fd < 0) return 0;

    int okay =
        write_all(fd, data, size) &&
        fchmod(fd, mode) == 0 &&
        close(fd) == 0;

    return okay;
}

static int make_full_ledger(const char *path) {
    int fd = open(
        path,
        O_WRONLY | O_CREAT | O_TRUNC,
        S_IRUSR | S_IWUSR
    );

    if (fd < 0) return 0;

    static const uint8_t header[] = "QNRL1\n";
    uint8_t record[QN_REPLAY_LEDGER_RECORD_BYTES];

    memset(record, 'a', 64u);
    record[64] = (uint8_t)'\n';

    if (!write_all(
            fd,
            header,
            sizeof(header) - 1u)) {
        close(fd);
        return 0;
    }

    for (size_t index = 0u;
         index < QN_REPLAY_LEDGER_MAX_RECORDS;
         ++index) {
        if (!write_all(fd, record, sizeof(record))) {
            close(fd);
            return 0;
        }
    }

    return close(fd) == 0;
}

static int expect_child_results(
    const char *path,
    bool unique
) {
    enum { CHILDREN = 8 };
    pid_t children[CHILDREN];

    for (int index = 0; index < CHILDREN; ++index) {
        pid_t child = fork();

        if (child < 0) {
            return 0;
        }

        if (child == 0) {
            uint8_t digest[
                QN_REPLAY_LEDGER_DIGEST_BYTES
            ];

            make_digest(
                digest,
                unique ?
                    (uint8_t)(80 + index) :
                    60u
            );

            bool consumed = false;
            QNDiagnostic diag = {0};

            QNStatus status =
                qn_replay_ledger_consume(
                    path,
                    digest,
                    &consumed,
                    &diag
                );

            if (status == QN_OK && consumed) {
                _exit(0);
            }

            if (!unique &&
                status == QN_ERR_RUNTIME &&
                !consumed &&
                strcmp(
                    diag.code,
                    "QN-E5204") == 0) {
                _exit(10);
            }

            _exit(20);
        }

        children[index] = child;
    }

    int success = 0;
    int replay = 0;
    int failure = 0;

    for (int index = 0; index < CHILDREN; ++index) {
        int child_status = 0;

        if (waitpid(
                children[index],
                &child_status,
                0) < 0 ||
            !WIFEXITED(child_status)) {
            ++failure;
            continue;
        }

        int code = WEXITSTATUS(child_status);

        if (code == 0) {
            ++success;
        } else if (code == 10) {
            ++replay;
        } else {
            ++failure;
        }
    }

    if (unique) {
        return
            success == CHILDREN &&
            replay == 0 &&
            failure == 0;
    }

    return
        success == 1 &&
        replay == CHILDREN - 1 &&
        failure == 0;
}

int main(void) {
    char directory_template[] =
        "/tmp/qnova-replay-ledger-XXXXXX";

    char *directory = mkdtemp(directory_template);

    if (!directory) {
        puts("FAIL: TEMP_DIRECTORY");
        return 1;
    }

    char ledger[QN_PATH_CAP];
    char empty_existing[QN_PATH_CAP];
    char malformed[QN_PATH_CAP];
    char oversized[QN_PATH_CAP];
    char full[QN_PATH_CAP];
    char exposed[QN_PATH_CAP];
    char target[QN_PATH_CAP];
    char symlink_path[QN_PATH_CAP];
    char hardlink_path[QN_PATH_CAP];
    char concurrent_same[QN_PATH_CAP];
    char concurrent_unique[QN_PATH_CAP];
    char missing_parent[QN_PATH_CAP];

#define BUILD_PATH(buffer, suffix) \
    do { \
        int written = snprintf( \
            buffer, \
            sizeof(buffer), \
            "%s/%s", \
            directory, \
            suffix \
        ); \
        if (written < 0 || \
            (size_t)written >= sizeof(buffer)) { \
            puts("FAIL: PATH_BUILD"); \
            return 1; \
        } \
    } while (0)

    BUILD_PATH(ledger, "ledger.qnrl");
    BUILD_PATH(empty_existing, "empty.qnrl");
    BUILD_PATH(malformed, "malformed.qnrl");
    BUILD_PATH(oversized, "oversized.qnrl");
    BUILD_PATH(full, "full.qnrl");
    BUILD_PATH(exposed, "exposed.qnrl");
    BUILD_PATH(target, "target.qnrl");
    BUILD_PATH(symlink_path, "symlink.qnrl");
    BUILD_PATH(hardlink_path, "hardlink.qnrl");
    BUILD_PATH(concurrent_same, "concurrent-same.qnrl");
    BUILD_PATH(concurrent_unique, "concurrent-unique.qnrl");
    BUILD_PATH(missing_parent, "missing/ledger.qnrl");

#undef BUILD_PATH

    uint8_t digest_a[QN_REPLAY_LEDGER_DIGEST_BYTES];
    uint8_t digest_b[QN_REPLAY_LEDGER_DIGEST_BYTES];
    uint8_t zero[QN_REPLAY_LEDGER_DIGEST_BYTES] = {0};

    make_digest(digest_a, 1u);
    make_digest(digest_b, 33u);

    bool seen = true;
    bool consumed = true;
    QNDiagnostic diag = {0};

    if (qn_replay_ledger_contains(
            ledger,
            digest_a,
            &seen,
            &diag) != QN_OK ||
        seen) {
        puts("FAIL: MISSING_LEDGER_NOT_EMPTY");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_contains(
            missing_parent,
            digest_a,
            &seen,
            &diag) != QN_ERR_IO ||
        strcmp(diag.code, "QN-E5202") != 0) {
        puts("FAIL: MISSING_PARENT_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_consume(
            ledger,
            digest_a,
            &consumed,
            &diag) != QN_OK ||
        !consumed) {
        puts("FAIL: FIRST_CONSUME");
        return 1;
    }

    struct stat ledger_metadata;

    if (stat(ledger, &ledger_metadata) != 0 ||
        !S_ISREG(ledger_metadata.st_mode) ||
        (ledger_metadata.st_mode & 0777) != 0600 ||
        ledger_metadata.st_nlink != 1 ||
        ledger_metadata.st_size !=
            (off_t)(
                QN_REPLAY_LEDGER_HEADER_BYTES +
                QN_REPLAY_LEDGER_RECORD_BYTES)) {
        puts("FAIL: CREATED_LEDGER_METADATA");
        return 1;
    }

    seen = false;
    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_contains(
            ledger,
            digest_a,
            &seen,
            &diag) != QN_OK ||
        !seen) {
        puts("FAIL: CONTAINS_AFTER_CONSUME");
        return 1;
    }

    consumed = true;
    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_consume(
            ledger,
            digest_a,
            &consumed,
            &diag) != QN_ERR_RUNTIME ||
        consumed ||
        strcmp(diag.code, "QN-E5204") != 0) {
        puts("FAIL: REPLAY_NOT_REJECTED");
        return 1;
    }

    if (stat(ledger, &ledger_metadata) != 0 ||
        ledger_metadata.st_size !=
            (off_t)(
                QN_REPLAY_LEDGER_HEADER_BYTES +
                QN_REPLAY_LEDGER_RECORD_BYTES)) {
        puts("FAIL: REPLAY_MUTATED_LEDGER");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_consume(
            ledger,
            digest_b,
            &consumed,
            &diag) != QN_OK ||
        !consumed) {
        puts("FAIL: SECOND_DIGEST_CONSUME");
        return 1;
    }

    int empty_fd = open(
        empty_existing,
        O_WRONLY | O_CREAT | O_EXCL,
        S_IRUSR | S_IWUSR
    );

    if (empty_fd < 0 || close(empty_fd) != 0) {
        puts("FAIL: EMPTY_FIXTURE");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_contains(
            empty_existing,
            digest_a,
            &seen,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5203") != 0) {
        puts("FAIL: EMPTY_EXISTING_LEDGER_ACCEPTED");
        return 1;
    }

    static const uint8_t malformed_data[] =
        "BAD!!\n";

    if (!write_bytes(
            malformed,
            malformed_data,
            sizeof(malformed_data) - 1u,
            S_IRUSR | S_IWUSR)) {
        puts("FAIL: MALFORMED_FIXTURE");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_contains(
            malformed,
            digest_a,
            &seen,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5203") != 0) {
        puts("FAIL: MALFORMED_LEDGER_ACCEPTED");
        return 1;
    }

    int oversized_fd = open(
        oversized,
        O_WRONLY | O_CREAT | O_TRUNC,
        S_IRUSR | S_IWUSR
    );

    if (oversized_fd < 0 ||
        ftruncate(
            oversized_fd,
            (off_t)QN_REPLAY_LEDGER_MAX_FILE_BYTES +
                1) != 0 ||
        close(oversized_fd) != 0) {
        puts("FAIL: OVERSIZED_FIXTURE");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_contains(
            oversized,
            digest_a,
            &seen,
            &diag) != QN_ERR_LIMIT ||
        strcmp(diag.code, "QN-E5205") != 0) {
        puts("FAIL: OVERSIZED_LEDGER_ACCEPTED");
        return 1;
    }

    if (!make_full_ledger(full)) {
        puts("FAIL: FULL_LEDGER_FIXTURE");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_consume(
            full,
            digest_a,
            &consumed,
            &diag) != QN_ERR_LIMIT ||
        strcmp(diag.code, "QN-E5205") != 0) {
        puts("FAIL: FULL_LEDGER_LIMIT_NOT_ENFORCED");
        return 1;
    }

    static const uint8_t valid_header[] =
        "QNRL1\n";

    if (!write_bytes(
            exposed,
            valid_header,
            sizeof(valid_header) - 1u,
            S_IRUSR | S_IWUSR |
                S_IRGRP | S_IROTH)) {
        puts("FAIL: EXPOSED_FIXTURE");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_contains(
            exposed,
            digest_a,
            &seen,
            &diag) != QN_ERR_IO ||
        strcmp(diag.code, "QN-E5202") != 0) {
        puts("FAIL: EXPOSED_LEDGER_ACCEPTED");
        return 1;
    }

    if (!write_bytes(
            target,
            valid_header,
            sizeof(valid_header) - 1u,
            S_IRUSR | S_IWUSR) ||
        symlink(target, symlink_path) != 0 ||
        link(target, hardlink_path) != 0) {
        puts("FAIL: LINK_FIXTURE");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_contains(
            symlink_path,
            digest_a,
            &seen,
            &diag) != QN_ERR_IO ||
        strcmp(diag.code, "QN-E5202") != 0) {
        puts("FAIL: SYMLINK_LEDGER_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_contains(
            hardlink_path,
            digest_a,
            &seen,
            &diag) != QN_ERR_IO ||
        strcmp(diag.code, "QN-E5202") != 0) {
        puts("FAIL: HARDLINK_LEDGER_ACCEPTED");
        return 1;
    }

    memset(&diag, 0, sizeof(diag));

    if (qn_replay_ledger_consume(
            ledger,
            zero,
            &consumed,
            &diag) != QN_ERR_RUNTIME ||
        strcmp(diag.code, "QN-E5201") != 0) {
        puts("FAIL: ZERO_DIGEST_ACCEPTED");
        return 1;
    }

    if (!expect_child_results(
            concurrent_same,
            false)) {
        puts("FAIL: CONCURRENT_REPLAY_ATOMICITY");
        return 1;
    }

    if (!expect_child_results(
            concurrent_unique,
            true)) {
        puts("FAIL: CONCURRENT_UNIQUE_CONSUME");
        return 1;
    }

    for (int index = 0; index < 8; ++index) {
        uint8_t digest[QN_REPLAY_LEDGER_DIGEST_BYTES];
        make_digest(digest, (uint8_t)(80 + index));

        seen = false;
        memset(&diag, 0, sizeof(diag));

        if (qn_replay_ledger_contains(
                concurrent_unique,
                digest,
                &seen,
                &diag) != QN_OK ||
            !seen) {
            puts("FAIL: CONCURRENT_UNIQUE_MISSING");
            return 1;
        }
    }

    (void)unlink(symlink_path);
    (void)unlink(hardlink_path);
    (void)unlink(target);
    (void)unlink(exposed);
    (void)unlink(full);
    (void)unlink(oversized);
    (void)unlink(malformed);
    (void)unlink(empty_existing);
    (void)unlink(ledger);
    (void)unlink(concurrent_same);
    (void)unlink(concurrent_unique);
    (void)rmdir(directory);

    puts(
        "PASS: "
        "QBIT_NOVA_REPLAY_LEDGER_FOUNDATION_"
        "V052_STEP4"
    );

    return 0;
}
