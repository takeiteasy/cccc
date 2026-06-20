// CCCC_FLAGS: --build --build-dry-run --build-target=core
// CCCC_EXPECT_STDOUT: ar rcs
// CCCC_REJECT_STDOUT: myapp
//
// --build-target=core prunes to that single target; the exe and dynamic lib
// must not appear in the dry-run output.

[[cccc::build]]
int build_main(void) {
    cccc_target_t *core = StaticLib("core");
    AddSource(core, "examples/build_demo/src/lib/sum.c");

    cccc_target_t *greet = DynamicLib("greet");
    AddSource(greet, "examples/build_demo/src/greet.c");

    cccc_target_t *app = Executable("myapp");
    AddSource(app, "examples/build_demo/src/main.c");
    LinkWith(app, core);
    LinkWith(app, greet);

    return BuildDefault();
}
