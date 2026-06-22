// CCCC_FLAGS: --build --build-dry-run --build-target=bc_plugin
// CCCC_EXPECT_STDOUT: \.c4d
//
// Smoke test for DynamicLib(kind=bytecode) (#564): the dry-run output must
// contain a .c4d path, confirming the bytecode dynamic module pipeline is
// used instead of the native cc -shared toolchain.

[[cccc::build_target(kind=bytecode)]]
BuildTarget *bc_plugin(Builder *ctx) {
    BuildTarget *t = DynamicLib(ctx, "plugin");
    AddSource(t, "examples/build_bytecode_libs_demo/src/plugin.c");
    return t;
}

[[cccc::build]]
int build_main(Builder *ctx) {
    return 0;
}
