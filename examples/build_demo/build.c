// Build script for the demo project. Run with:
//
//     cccc --build examples/build_demo/build.c
//
// It declares three native targets — a static library, a dynamic library, and
// an executable that links against both — and builds them with the system
// toolchain. The same file is an ordinary C source: the [[cccc::build]] entry
// is only invoked in --build mode.

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *inc = "examples/build_demo/include";

    BuildTarget *core = StaticLib(ctx, "core");
    AddSource(core, "examples/build_demo/src/lib/sum.c");
    AddInclude(core, inc);

    BuildTarget *greet = DynamicLib(ctx, "greet");
    AddSource(greet, "examples/build_demo/src/greet.c");
    AddInclude(greet, inc);

    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddInclude(app, inc);
    AddDefine(app, "GREET_DEFAULT", "\"build mode\"");
    LinkWith(app, core);
    LinkWith(app, greet);

    return BuildDefault(ctx);
}
