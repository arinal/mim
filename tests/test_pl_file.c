// SPDX-License-Identifier: MIT OR GPL-2.0-only

#define _GNU_SOURCE // mkstemp, unlink

#include "mim_test.h"
#include "platform/pl.h"

#include <string.h>
#include <unistd.h>

static void write_file(const char * const path, const char * const data, const size_t len) {
    FILE * const f = fopen(path, "wb");
    CHECK(f != NULL);
    CHECK(fwrite(data, 1, len, f) == len);
    CHECK(fclose(f) == 0);
}

int main(void) {
    char path[] = "/tmp/mim_test_XXXXXX";
    const int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);

    const char data[] = "hello, mim";
    const size_t data_len = sizeof(data) - 1;
    write_file(path, data, data_len);

    size_t size;
    CHECK(pl_file_size(path, &size) == pl_result_ok);
    CHECK(size == data_len);

    char buf[64];
    size_t got;
    CHECK(pl_file_read(path, buf, sizeof(buf), &got) == pl_result_ok);
    CHECK(got == data_len && memcmp(buf, data, data_len) == 0);

    // exactly fits, one byte short
    CHECK(pl_file_read(path, buf, data_len, &got) == pl_result_ok && got == data_len);
    CHECK(pl_file_read(path, buf, data_len - 1, &got) == pl_result_too_big);

    write_file(path, "", 0);
    CHECK(pl_file_read(path, buf, sizeof(buf), &got) == pl_result_ok && got == 0);
    CHECK(pl_file_read(path, NULL, 0, &got) == pl_result_ok && got == 0);

    CHECK(unlink(path) == 0);
    CHECK(pl_file_size(path, &size) == pl_result_not_found);
    CHECK(pl_file_read(path, buf, sizeof(buf), &got) == pl_result_not_found);

    // a directory is not a file
    CHECK(pl_file_size("/tmp", &size) == pl_result_io_error);
    CHECK(pl_file_read("/tmp", buf, sizeof(buf), &got) == pl_result_io_error);
    return 0;
}
