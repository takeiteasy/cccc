// CCCC_FLAGS: --build --build-dry-run --build-target=bc_app
// CCCC_EXPECT_STDOUT: --link[^\n]*mathlib\.c4a
//
// Regression test for StaticLib(kind=bytecode) as a LinkWith dep (#563 + #565).
// A kind=bytecode Executable that links a StaticLib(kind=bytecode) dep must
// build the static lib as a standalone .c4a and link it via --link in the exe's
// cccc invocation (#565, replaces source-folding for exe targets).
// The discriminating regex verifies that --link and mathlib.c4a appear on the
// same invocation line (the exe's compile).

[[cccc::build_target(kind = bytecode)]]
BuildTarget *bc_app(Builder *ctx) {
    // Static library: defines lib_add/lib_mul, no main().
    BuildTarget *lib = StaticLib(ctx, "mathlib");
    AddSource(lib, "tests/fixtures/build_bytecode_libs_demo/src/math_lib.c");
    AddInclude(lib, "tests/fixtures/build_bytecode_libs_demo/include");

    // Executable: calls lib functions.
    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "tests/fixtures/build_bytecode_libs_demo/src/main.c");
    AddInclude(app, "tests/fixtures/build_bytecode_libs_demo/include");
    LinkWith(app, lib);
    return app;
}

[[cccc::build]]
int build_main(Builder *ctx) {
    return 0;
}
