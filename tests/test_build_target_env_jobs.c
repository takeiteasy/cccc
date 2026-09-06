// CCCC_FLAGS: --build --build-jobs=4 --build-out-dir=build/test_target_env_jobs_out
// CCCC_EXPECT_STDOUT: saw CCCC_TEST_ENV_MARKER=hello_from_pool
// CCCC_EXPECT_STDOUT: build succeeded
//
// #1309: under --build-jobs>1 the parallel source-compile pool forks and
// execs the compiler directly, bypassing run_step()/run_argv_env() -- the
// only place SetTargetEnv (t->env) was applied. So a per-target environment
// override was silently dropped for the compile stage under -j. This test
// has two sources (so the pool path runs) and asserts the wrapper "compiler"
// still sees the variable. Modelled on test_build_set_target_env.

[[cccc::build]]
int build_main(Builder *ctx) {
    WriteFile(ctx, "build/gen/pool_echo_cc.sh",
              "#!/bin/sh\n"
              "echo \"saw CCCC_TEST_ENV_MARKER=$CCCC_TEST_ENV_MARKER\"\n"
              "exec cc \"$@\"\n");
    WriteFile(ctx, "build/gen/pool_env_a.c", "int a(void) { return 1; }\n");
    WriteFile(ctx, "build/gen/pool_env_b.c",
              "int a(void); int main(void) { return a() - 1; }\n");
    BuildTarget *chmod_step = RunCustom(ctx, "chmod_pool_echo_cc",
                                        "chmod +x build/gen/pool_echo_cc.sh");

    BuildTarget *app = Executable(ctx, "pool_env_app");
    AddSource(app, "build/gen/pool_env_a.c");
    AddSource(app, "build/gen/pool_env_b.c");
    SetToolchain(app, "build/gen/pool_echo_cc.sh");
    SetTargetEnv(app, "CCCC_TEST_ENV_MARKER", "hello_from_pool");
    DependsOn(app, chmod_step);

    return BuildDefault(ctx);
}
