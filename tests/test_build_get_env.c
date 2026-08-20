// CCCC_FLAGS: --build
//
// GetEnv: return an environment variable value, or NULL if unset.
// PATH is always set in any environment that can run the test suite.

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *path = GetEnv(ctx, "PATH");
    if (!path || path[0] == '\0')
        return 1;

    const char *missing =
        GetEnv(ctx, "CCCC_TEST_VAR_DEFINITELY_NOT_SET_XYZ123");
    if (missing != (void *)0)
        return 1;

    return 42;
}
