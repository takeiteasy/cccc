// CCCC_FLAGS: --build --build-out-dir=build/test_cccc_pragma_link_out
// CCCC_EXPECT_STDOUT: (?=[\s\S]*cccc_pragma_link_ran: 3)(?=[\s\S]*build succeeded)
//
// #1133 / #1149: a source that queues a library via `#pragma cccc link(...)`
// must still work when it reaches cccc through a CcccExecutable target.
// build.c only lists the source files on the cccc command line; the pragma
// is handled inside cccc exactly as for a direct `cccc --compile=native`
// invocation. Builds a program that calls a <math.h> function and runs it.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    WriteFile(ctx, "build/gen/pragma_link_src.c",
              "#pragma cccc link(\"m\")\n"
              "#include <math.h>\n"
              "#include <stdio.h>\n"
              "int main(void) {\n"
              "    printf(\"cccc_pragma_link_ran: %d\\n\", (int)sqrt(9.0));\n"
              "    return 0;\n"
              "}\n");

    BuildTarget *app = CcccExecutable(ctx, "cccc_pragma_link_app");
    AddSource(app, "build/gen/pragma_link_src.c");

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s", TargetOutput(app));
    BuildTarget *run = RunCustom(ctx, "run-pragma-link", cmd);
    DependsOn(run, app);

    return BuildDefault(ctx);
}
