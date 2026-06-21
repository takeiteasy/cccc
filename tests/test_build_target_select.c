// CCCC_FLAGS: --build --build-dry-run --build-target=core
// CCCC_EXPECT_STDOUT: ar rcs
// CCCC_REJECT_STDOUT: myapp
//
// --build-target=core prunes to that single target; the exe and dynamic lib
// must not appear in the dry-run output.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *core = StaticLib(ctx, "core");
    AddSource(core, "examples/build_demo/src/lib/sum.c");

    BuildTarget *greet = DynamicLib(ctx, "greet");
    AddSource(greet, "examples/build_demo/src/greet.c");

    BuildTarget *app = Executable(ctx, "myapp");
    AddSource(app, "examples/build_demo/src/main.c");
    LinkWith(app, core);
    LinkWith(app, greet);

    return BuildDefault(ctx);
}
