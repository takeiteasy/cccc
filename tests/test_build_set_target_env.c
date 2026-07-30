// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: saw CCCC_TEST_ENV_MARKER=hello_from_build_script
// CCCC_EXPECT_STDOUT: build succeeded
//
// #842: SetTargetEnv() puts an environment variable in the compiler child's
// environment only (threaded through run_step()/run_argv_env()) -- the case
// this exists for is AFL_USE_ASAN=1, which afl-clang-fast reads at
// invocation time to decide whether to link ASan. Verified with a small
// shell-script "compiler" (wired in via SetToolchain) that echoes the
// variable before delegating to the real cc.

[[cccc::build]]
int build_main(Builder *ctx) {
    WriteFile(ctx, "build/gen/echo_cc.sh",
        "#!/bin/sh\n"
        "echo \"saw CCCC_TEST_ENV_MARKER=$CCCC_TEST_ENV_MARKER\"\n"
        "exec cc \"$@\"\n");
    WriteFile(ctx, "build/gen/target_env_main.c", "int main(void) { return 0; }\n");
    BuildTarget *chmod_step = RunCustom(ctx, "chmod_echo_cc", "chmod +x build/gen/echo_cc.sh");

    BuildTarget *app = Executable(ctx, "envapp");
    AddSource(app, "build/gen/target_env_main.c");
    SetToolchain(app, "build/gen/echo_cc.sh");
    SetTargetEnv(app, "CCCC_TEST_ENV_MARKER", "hello_from_build_script");
    DependsOn(app, chmod_step);

    return BuildDefault(ctx);
}
