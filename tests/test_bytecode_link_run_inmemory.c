// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: exit=42
//
// Functional test for #898: `--link lib.c4a` without `-o` must link the
// library into the in-memory image and *run* it, not fall through to
// executing an unlinked (and therefore corrupt) image. Before the fix this
// crashed immediately with a bogus "STACK OVERFLOW" from a CALL whose
// operand had been left at its unpatched placeholder value of 0.
//
// build_main uses CaptureCommand to invoke cccc directly for each step
// (compile lib to .c4a, then run the main source directly with --link and
// no -o) so the test is self-contained on a clean build.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char        lib_out[512], cmd[1024];
    snprintf(lib_out, sizeof(lib_out), "%s/lib/mathlib_link_inmem_test.c4a",
             out_dir);

    snprintf(cmd, sizeof(cmd),
             "mkdir -p %s/lib && ./cccc "
             "tests/fixtures/build_bytecode_libs_demo/src/math_lib.c "
             "-I tests/fixtures/build_bytecode_libs_demo/include "
             "--compile=bytecode -o %s 2>&1",
             out_dir, lib_out);
    if (!CaptureCommand(ctx, cmd))
        return 1;

    // No -o here: this is the exact repro from #898.
    snprintf(cmd, sizeof(cmd),
             "sh -c './cccc tests/fixtures/build_bytecode_libs_demo/src/main.c "
             "-I tests/fixtures/build_bytecode_libs_demo/include --link %s "
             ">/dev/null 2>&1; echo \"exit=$?\"'",
             lib_out);
    const char *result = CaptureCommand(ctx, cmd);
    if (result)
        puts(result);
    return 0;
}
