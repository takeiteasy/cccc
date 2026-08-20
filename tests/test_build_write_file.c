// CCCC_FLAGS: --build
//
// WriteFile: write a string to a file, creating parent directories as needed.

#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *tmp      = "/tmp/cccc_write_file_test_559/hello.txt";
    const char *expected = "hello from WriteFile";

    // Write the file (directory is created automatically).
    if (WriteFile(ctx, tmp, expected) != 0)
        return 1;

    // FileExists must agree the file now exists.
    if (!FileExists(ctx, tmp))
        return 1;

    // ReadFile must return the same content.
    const char *got = ReadFile(ctx, tmp);
    if (!got || strcmp(got, expected) != 0)
        return 1;

    // NULL path/content should fail gracefully.
    if (WriteFile(ctx, (void *)0, "x") != -1)
        return 1;
    if (WriteFile(ctx, tmp, (void *)0) != -1)
        return 1;

    return 42;
}
