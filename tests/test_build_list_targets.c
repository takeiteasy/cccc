// CCCC_FLAGS: --build --build-list-targets
// CCCC_EXPECT_STDOUT: make_lib
// CCCC_EXPECT_STDOUT: make_app
//
// --build-list-targets prints the names of all [[cccc::build_target]] factories
// and exits without invoking build_main.

[[cccc::build_target]]
BuildTarget *make_lib(Builder *ctx) {
    BuildTarget *t = StaticLib(ctx, "mylib");
    AddSource(t, "lib.c");
    return t;
}

[[cccc::build_target(kind=native)]]
BuildTarget *make_app(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "myapp");
    AddSource(t, "main.c");
    return t;
}

[[cccc::build]]
int build_main(Builder *ctx) {
    // Should NOT run with --build-list-targets.
    return BuildDefault(ctx);
}
