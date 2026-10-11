// CCCC_FLAGS: --build --build-dry-run --build-target=app
// CCCC_EXPECT_STDOUT: app output: build/bin/app
// CCCC_EXPECT_STDOUT: gen output: src/generated.inc
//
// #842: TargetOutput() resolves the on-disk path a target will produce.
// For a compiled target (exe/static/dynamic) that's <out_dir>/<path>
// (explicit SetOutput() or the kind-appropriate default). For a RunCustom
// target it's whatever DeclareOutput() recorded, taken verbatim (not joined
// onto out_dir) -- a custom command can write anywhere, e.g. a generator
// writing directly into src/, not under build/.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *gen = RunCustom(ctx, "gen", "true");
    DeclareOutput(gen, "src/generated.inc");

    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "examples/build_demo/src/main.c");
    DependsOn(app, gen);

    printf("app output: %s\n", TargetOutput(app));
    printf("gen output: %s\n", TargetOutput(gen));

    return BuildDefault(ctx);
}
