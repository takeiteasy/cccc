// CCCC_FLAGS: --build
//
// #569: SetCwd / GetCwd build API helpers.

#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    // GetCwd returns a non-NULL string.
    const char *orig = GetCwd(ctx);
    if (!orig || orig[0] == '\0')
        return 1;

    // SetCwd to /tmp succeeds.
    if (SetCwd(ctx, "/tmp") != 0)
        return 1;

    // GetCwd now reflects the new directory.
    const char *after = GetCwd(ctx);
    if (!after)
        return 1;
    // On macOS /tmp is a symlink to /private/tmp; accept either.
    if (strcmp(after, "/tmp") != 0 && strcmp(after, "/private/tmp") != 0)
        return 1;

    // Restore the original directory manually (auto-restore happens at teardown
    // but we want GetCwd to be usable again here too).
    if (SetCwd(ctx, orig) != 0)
        return 1;
    const char *restored = GetCwd(ctx);
    if (!restored || strcmp(restored, orig) != 0)
        return 1;

    return 42;
}
