// CCCC_FLAGS: --build
//
// ReadFile: read a small file into a NUL-terminated string.

#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    // Read a file that is known to exist — use this test file itself via BuildRoot.
    // Since the test is run from the repo root, tests/ is accessible.
    const char *content = ReadFile(ctx, "tests/test_build_read_file.c");
    if (!content) return 1;

    // The file must contain its own marker comment.
    if (!strstr(content, "ReadFile")) return 1;

    // Non-existent file returns NULL.
    const char *none = ReadFile(ctx, "/this/does/not/exist/cccc_xyz999.c");
    if (none != (void*)0) return 1;

    return 42;
}
