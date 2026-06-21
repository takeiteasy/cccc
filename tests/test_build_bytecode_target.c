// CCCC_FLAGS: --build --build-dry-run --build-target=bc_exe
// CCCC_EXPECT_STDOUT: \.c4
//
// Smoke test for kind=bytecode build targets (#545): the dry-run output must
// contain a .c4 path, confirming the bytecode compilation pipeline is used
// instead of the native cc/ar/ld toolchain.

[[cccc::build_target(kind=bytecode)]]
BuildTarget *bc_exe(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "hello");
    AddSource(t, "examples/build_demo/src/main.c");
    AddSource(t, "examples/build_demo/src/greet.c");
    AddSource(t, "examples/build_demo/src/lib/sum.c");
    AddInclude(t, "examples/build_demo/include");
    return t;
}

[[cccc::build]]
int build_main(Builder *ctx) {
    return 0;
}
