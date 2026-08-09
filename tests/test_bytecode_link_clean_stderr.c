// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: OK
// CCCC_REJECT_STDOUT: return-buffer pool
//
// Regression for #899: a clean `--link` build must not print
// "cc_load_module: return-buffer pool full; module return buffers not
// added" to stderr. That warning fired unconditionally on every successful
// --link because the merge loop compared against a pool that a normal host
// VM always starts with fully populated (it's a fixed-size rotating
// scratch pool, not a per-module resource list -- see the fix in
// cc_load_module(), src/bytecode.c).
//
// Unlike the other --link tests (which use `2>&1`, merging stderr into
// stdout so a stray warning there wouldn't be noticed), this test captures
// stderr *alone* via `2>&1 >/dev/null` so the assertion actually exercises
// the bug.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char lib_out[512], exe_out[512], cmd[1024];
    snprintf(lib_out, sizeof(lib_out), "%s/lib/mathlib_link_stderr_test.c4a", out_dir);
    snprintf(exe_out, sizeof(exe_out), "%s/bin/app_link_stderr_test.c4", out_dir);

    snprintf(cmd, sizeof(cmd),
        "mkdir -p %s/lib && ./cccc "
        "tests/fixtures/build_bytecode_libs_demo/src/math_lib.c "
        "-I tests/fixtures/build_bytecode_libs_demo/include "
        "--compile=bytecode -o %s 2>&1",
        out_dir, lib_out);
    if (!CaptureCommand(ctx, cmd)) return 1;

    snprintf(cmd, sizeof(cmd),
        "mkdir -p %s/bin && sh -c "
        "'./cccc tests/fixtures/build_bytecode_libs_demo/src/main.c "
        "-I tests/fixtures/build_bytecode_libs_demo/include --link %s -o %s "
        "2>&1 >/dev/null'",
        out_dir, lib_out, exe_out);
    const char *stderr_only = CaptureCommand(ctx, cmd);

    if (stderr_only && strstr(stderr_only, "return-buffer pool")) {
        printf("FAIL: unexpected stderr: %s\n", stderr_only);
        return 1;
    }
    puts("OK");
    return 0;
}
