// CCCC_FLAGS: --build
//
// CaptureCommand: run a shell command and return its stdout (trailing
// whitespace stripped) as a NUL-terminated string.

#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out = CaptureCommand(ctx, "echo hello");
    if (!out)
        return 1;
    if (strcmp(out, "hello") != 0)
        return 1;

    // Trailing newline is stripped
    const char *out2 = CaptureCommand(ctx, "printf 'world\\n'");
    if (!out2 || strcmp(out2, "world") != 0)
        return 1;

    // Failed command returns NULL
    const char *fail = CaptureCommand(ctx, "exit 1");
    if (fail != (void *)0)
        return 1;

    return 42;
}
