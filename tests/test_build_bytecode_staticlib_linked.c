// CCCC_FLAGS: --build --build-dry-run --build-target=bc_app
// CCCC_EXPECT_STDOUT: \.c4a
//
// Regression test for bytecode-linker build pass (#565): a kind=bytecode
// Executable with a StaticLib LinkWith dep must produce two separate cccc
// invocations — one building the .c4a, one building the .c4 with --link.
// The regex checks that a .c4a artifact appears in the dry-run output.

[[cccc::build_target(kind=bytecode)]]
BuildTarget *bc_app(Builder *ctx) {
    BuildTarget *lib = StaticLib(ctx, "mathlib");
    AddSource(lib, "tests/fixtures/build_bytecode_libs_demo/src/math_lib.c");
    AddInclude(lib, "tests/fixtures/build_bytecode_libs_demo/include");

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
