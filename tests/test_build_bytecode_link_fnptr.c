// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: exit=42
//
// Functional test for cross-module function-pointer decay (#566): build a .c4a
// static lib and a .c4 exe where the exe calls lib_add via a function pointer.
// math_lib.c defines lib_add(int,int); fnptr_main.c stores lib_add in a
// function pointer fp and calls fp(40,2), expecting the return value 42.
//
// build_main uses CaptureCommand to invoke cccc directly for each step so
// the test is self-contained on a clean build without relying on cached
// artifacts.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *out_dir = BuildOutDir(ctx);
    char lib_out[512], exe_out[512], cmd[1024];
    snprintf(lib_out, sizeof(lib_out), "%s/lib/mathlib_fnptr_test.c4a", out_dir);
    snprintf(exe_out, sizeof(exe_out), "%s/bin/app_fnptr_test.c4", out_dir);

    snprintf(cmd, sizeof(cmd),
        "mkdir -p %s/lib && ./cccc "
        "examples/build_bytecode_libs_demo/src/math_lib.c "
        "-I examples/build_bytecode_libs_demo/include "
        "--compile=bytecode -o %s 2>&1",
        out_dir, lib_out);
    if (!CaptureCommand(ctx, cmd)) return 1;

    snprintf(cmd, sizeof(cmd),
        "mkdir -p %s/bin && ./cccc "
        "examples/build_bytecode_libs_demo/src/fnptr_main.c "
        "-I examples/build_bytecode_libs_demo/include "
        "--link %s -o %s 2>&1",
        out_dir, lib_out, exe_out);
    if (!CaptureCommand(ctx, cmd)) return 1;

    snprintf(cmd, sizeof(cmd),
        "sh -c '\"./cccc\" \"%s\"; echo \"exit=$?\"'", exe_out);
    const char *result = CaptureCommand(ctx, cmd);
    if (result) puts(result);
    return 0;
}
