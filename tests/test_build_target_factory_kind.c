// CCCC_FLAGS: --build --build-dry-run --build-target=my_target
// CCCC_EXPECT_STDOUT: bin/app
//
// kind=native is the only valid [[cccc::build_target]] option (on-disk
// bytecode targets were removed, #1215). Dry-run must still print the
// factory-returned target's native executable output path.

[[cccc::build_target(kind = native)]]
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
