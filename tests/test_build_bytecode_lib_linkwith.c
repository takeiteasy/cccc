// CCCC_FLAGS: --build --build-dry-run --build-target=bc_app
// CCCC_EXPECT_STDOUT: cccc[^\n]*math_lib\.c[^\n]*\.c4
//
// Regression test for StaticLib(kind=bytecode) as a LinkWith dep (#564).
// A kind=bytecode Executable that links a StaticLib(kind=bytecode) dep must
// fold the static lib's sources into its single cccc invocation (same as
// Executable deps in #563).  The discriminating regex verifies that
// math_lib.c and the .c4 output appear on the same cccc invocation line.

[[cccc::build_target(kind=bytecode)]]
BuildTarget *bc_app(Builder *ctx) {
    // Static library: defines lib_add/lib_mul, no main().
    BuildTarget *lib = StaticLib(ctx, "mathlib");
    AddSource(lib, "examples/build_bytecode_libs_demo/src/math_lib.c");
    AddInclude(lib, "examples/build_bytecode_libs_demo/include");

    // Executable: calls lib functions.
    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "examples/build_bytecode_libs_demo/src/main.c");
    AddInclude(app, "examples/build_bytecode_libs_demo/include");
    LinkWith(app, lib);
    return app;
}

[[cccc::build]]
int build_main(Builder *ctx) {
    return 0;
}
