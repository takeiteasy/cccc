// CCCC_FLAGS: --build
// CCCC_RUN_ARGS: hello world
//
// BuildArgc / BuildArgv: user args forwarded to the build entry via the -- separator.

#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    // Two args: "hello" and "world"
    if (BuildArgc(ctx) != 2) return 1;
    if (strcmp(BuildArgv(ctx, 0), "hello") != 0) return 2;
    if (strcmp(BuildArgv(ctx, 1), "world") != 0) return 3;

    // Out-of-bounds access returns NULL
    if (BuildArgv(ctx, 2) != (void*)0) return 4;
    if (BuildArgv(ctx, -1) != (void*)0) return 5;

    return 42;
}
