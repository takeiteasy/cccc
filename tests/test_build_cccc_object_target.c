// CCCC_FLAGS: --build --build-out-dir=build/test_cccc_object_out
// CCCC_EXPECT_STDOUT: --compile=object
// CCCC_EXPECT_STDOUT: cccc_object_ran: sum = 10
// CCCC_EXPECT_STDOUT: build succeeded
//
// A CcccObject target is compiled by the running cccc into obj/<name>.o and a
// plain Executable links it through LinkWith.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *obj = CcccObject(ctx, "cccc_object_sum");
    AddSource(obj, "examples/build_demo/src/lib/sum.c");
    AddInclude(obj, "examples/build_demo/include");

    BuildTarget *app = Executable(ctx, "cccc_object_app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddInclude(app, "examples/build_demo/include");
    LinkWith(app, obj);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s | tail -n1 | sed 's/^/cccc_object_ran: /'",
             TargetOutput(app));
    BuildTarget *run = RunCustom(ctx, "run-cccc-object", cmd);
    DependsOn(run, app);

    return BuildDefault(ctx);
}
