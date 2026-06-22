// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: exit=42
//
// Functional test for the bytecode linker pass (#565): build a .c4a static
// lib and a .c4 exe via the bytecode linker, then run the exe and verify the
// result.  math_lib.c defines lib_add(int,int); main.c calls lib_add(40,2).

#include <stdio.h>
#include <string.h>

[[cccc::build_target(kind=bytecode)]]
BuildTarget *bc_app_link(Builder *ctx) {
    BuildTarget *lib = StaticLib(ctx, "mathlib_link_test");
    AddSource(lib, "examples/build_bytecode_libs_demo/src/math_lib.c");
    AddInclude(lib, "examples/build_bytecode_libs_demo/include");

    BuildTarget *app = Executable(ctx, "app_link_test");
    AddSource(app, "examples/build_bytecode_libs_demo/src/main.c");
    AddInclude(app, "examples/build_bytecode_libs_demo/include");
    LinkWith(app, lib);
    return app;
}

[[cccc::build]]
int build_main(Builder *ctx) {
    int rc = BuildDefault(ctx);
    if (rc != 0) return rc;
    const char *out_dir = BuildOutDir(ctx);
    char cmd[512];
    // Run the linked exe; it should return 42. Capture that as stdout text.
    snprintf(cmd, sizeof(cmd),
        "sh -c '\"./cccc\" \"%s/bin/app_link_test.c4\"; echo \"exit=$?\"'",
        out_dir);
    const char *result = CaptureCommand(ctx, cmd);
    if (result) puts(result);
    return 0;
}
