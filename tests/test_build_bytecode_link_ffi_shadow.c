// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: exit=42
//
// Functional test for the cross-module FFI-shadow fix (#882): a guest
// module can define its own `abs`, compiled standalone to a .c4a bytecode
// library; another module that only *declares* abs (no body -- the ordinary
// libc-header shape) and links against that library must call the guest's
// own definition, not silently fall back to the host FFI abs() (which
// find_ffi_function's exact-name match alone can't tell apart from a
// cross-module guest definition it can't see).
//
// tests/fixtures/ffi_shadow_lib_882.c's abs() always returns 42 regardless
// of its argument; the host FFI abs(-1) would return 1. So exit=42 proves
// the guest definition won.
//
// build_main uses CaptureCommand to invoke cccc directly for each step
// (compile lib to .c4a, link exe, run exe) so the test is self-contained
// on a clean build without relying on cached artifacts. Modelled on
// tests/test_build_bytecode_link_fnptr.c.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char        lib_out[512], exe_out[512], cmd[1024];
    snprintf(lib_out, sizeof(lib_out), "%s/lib/ffi_shadow_test.c4a", out_dir);
    snprintf(exe_out, sizeof(exe_out), "%s/bin/ffi_shadow_test.c4", out_dir);

    snprintf(cmd, sizeof(cmd),
             "mkdir -p %s/lib && ./cccc "
             "tests/fixtures/ffi_shadow_lib_882.c "
             "--compile=bytecode -o %s 2>&1",
             out_dir, lib_out);
    if (!CaptureCommand(ctx, cmd))
        return 1;

    snprintf(cmd, sizeof(cmd),
             "mkdir -p %s/bin && ./cccc "
             "tests/fixtures/ffi_shadow_main_882.c "
             "--link %s -o %s 2>&1",
             out_dir, lib_out, exe_out);
    if (!CaptureCommand(ctx, cmd))
        return 1;

    snprintf(cmd, sizeof(cmd), "sh -c '\"./cccc\" \"%s\"; echo \"exit=$?\"'",
             exe_out);
    const char *result = CaptureCommand(ctx, cmd);
    if (result)
        puts(result);
    return 0;
}
