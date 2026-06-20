// CCCC_FLAGS: --build --build-dry-run --build-target=app
// CCCC_EXPECT_STDOUT: ar rcs
// CCCC_EXPECT_STDOUT: -shared
// CCCC_EXPECT_STDOUT: build succeeded \(3 targets, 0 errors\)
//
// --build-target=app keeps transitive deps (core static lib + greet dynamic
// lib) because app links_with both.  All three targets must be built.

[[cccc::build]]
int build_main(void) {
    cccc_target_t *core = StaticLib("core");
    AddSource(core, "examples/build_demo/src/lib/sum.c");

    cccc_target_t *greet = DynamicLib("greet");
    AddSource(greet, "examples/build_demo/src/greet.c");

    cccc_target_t *app = Executable("app");
    AddSource(app, "examples/build_demo/src/main.c");
    LinkWith(app, core);
    LinkWith(app, greet);

    return BuildDefault();
}
