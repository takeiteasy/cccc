// CCCC_FLAGS: --build --build-dry-run --build-target=app_factory
// CCCC_EXPECT_STDOUT: factory_app\.c
// CCCC_REJECT_STDOUT: entry_main\.c
//
// When --build-target=NAME matches a [[cccc::build_target]] factory, the
// factory is invoked directly and build_main is skipped entirely.  The dry-run
// output must contain the factory's source and must NOT contain build_main's
// source.

[[cccc::build_target]]
BuildTarget *app_factory(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app_from_factory");
    AddSource(t, "factory_app.c");
    return t;
}

[[cccc::build]]
int build_main(Builder *ctx) {
    // This should NOT run when --build-target=app_factory matches the factory.
    BuildTarget *t = Executable(ctx, "from_entry");
    AddSource(t, "entry_main.c");
    return BuildDefault(ctx);
}
