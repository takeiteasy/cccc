// CCCC_FLAGS: --build --build-out-dir=build/test_cccc_target_env_out
// CCCC_EXPECT_STDOUT: (?=[\s\S]*saw CCCC_NATIVE_CC wrapper)(?=[\s\S]*cccc_target_env_ok)(?=[\s\S]*build succeeded)
//
// #1133: SetTargetEnv on a CcccExecutable target reaches the child process,
// which IS cccc — so SetTargetEnv(t, "CCCC_NATIVE_CC", ...) picks the host
// compiler cccc uses to compile and link its emitted C. Verified with a
// shell-script "compiler" that announces itself and delegates to the real
// cc. Modelled on test_build_set_target_env.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    WriteFile(ctx, "build/gen/cccc_env_cc.sh",
              "#!/bin/sh\n"
              "echo \"saw CCCC_NATIVE_CC wrapper\"\n"
              "exec cc \"$@\"\n");
    BuildTarget *chmod_step = RunCustom(ctx, "chmod_cccc_env_cc",
                                        "chmod +x build/gen/cccc_env_cc.sh");

    BuildTarget *app        = CcccExecutable(ctx, "cccc_target_env_app");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/main.c");
    AddInclude(app, "examples/build_demo/include");
    SetTargetEnv(app, "CCCC_NATIVE_CC", "build/gen/cccc_env_cc.sh");
    DependsOn(app, chmod_step);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s | tail -n1 | sed 's/.*/cccc_target_env_ok/'",
             TargetOutput(app));
    BuildTarget *run = RunCustom(ctx, "run-cccc-target-env", cmd);
    DependsOn(run, app);

    return BuildDefault(ctx);
}
