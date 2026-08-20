// CCCC_FLAGS: --build --build-dry-run --build-target=bc_mathlib
// CCCC_EXPECT_STDOUT: \.c4a
//
// Smoke test for StaticLib(kind=bytecode) (#564): the dry-run output must
// contain a .c4a path, confirming the bytecode library pipeline is used
// instead of the native ar toolchain.  Also verifies that -c bytecode is
// passed so cccc does not require main().

[[cccc::build_target(kind = bytecode)]]
BuildTarget *bc_mathlib(Builder *ctx) {
    BuildTarget *t = StaticLib(ctx, "mathlib");
    AddSource(t, "tests/fixtures/build_bytecode_libs_demo/src/math_lib.c");
    AddInclude(t, "tests/fixtures/build_bytecode_libs_demo/include");
    return t;
}

[[cccc::build]]
int build_main(Builder *ctx) {
    return 0;
}
