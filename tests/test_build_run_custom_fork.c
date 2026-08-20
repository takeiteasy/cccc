// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: cwd_isolation_ok
//
// #568 regression: RunCustom must run the shell in a forked child.
// We verify by checking that `cd` inside RunCustom does NOT change the
// parent process's CWD.  Under the old posix_shell_inline path, chdir()
// ran in the parent; under posix_shell_with_io (forked) it runs in the
// child and is invisible to the parent.
//
// Note: the OOM die() path is not unit-testable; this validates fork isolation.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    // Save parent CWD before the RunCustom step.
    const char *before = GetCwd(ctx);
    if (!before)
        return 1;

    // RunCustom with `cd /tmp`.  Under the old inline path this would change
    // the parent CWD; under the forked path it stays in the child.
    BuildTarget *step = RunCustom(ctx, "cd_noop", "cd /tmp");
    if (!step)
        return 1;
    if (Build(ctx, step) != 0)
        return 1;

    // Parent CWD must be unchanged.
    const char *after = GetCwd(ctx);
    if (!after)
        return 1;

    if (strcmp(before, after) == 0)
        printf("cwd_isolation_ok\n");

    return 42;
}
