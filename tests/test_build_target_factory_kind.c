// CCCC_FLAGS: --build --build-dry-run --build-target=my_target
// CCCC_EXPECT_STDOUT: \.c4
//
// kind=bytecode is now a valid [[cccc::build_target]] option (#545).
// Dry-run must print the cccc invocation with a .c4 output path.

[[cccc::build_target(kind = bytecode)]]
BuildTarget *my_target(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app");
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
