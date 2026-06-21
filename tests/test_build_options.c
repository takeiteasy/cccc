// CCCC_FLAGS: --build --build-option=foo=bar --build-option=baz=1 --build-option=empty=
//
// GetBuildOption / HaveBuildOption: query Zig-style typed build options
// passed via --build-option=key=value on the CLI.

#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    // Known key → value
    const char *foo = GetBuildOption(ctx, "foo");
    if (!foo || strcmp(foo, "bar") != 0) return 1;

    const char *baz = GetBuildOption(ctx, "baz");
    if (!baz || strcmp(baz, "1") != 0) return 1;

    // Key with empty value
    const char *empty = GetBuildOption(ctx, "empty");
    if (!empty || empty[0] != '\0') return 1;

    // Unknown key returns NULL
    const char *missing = GetBuildOption(ctx, "not_set_xyz");
    if (missing != (void*)0) return 1;

    // HaveBuildOption
    if (!HaveBuildOption(ctx, "foo")) return 1;
    if (!HaveBuildOption(ctx, "baz")) return 1;
    if (HaveBuildOption(ctx, "not_set_xyz")) return 1;

    return 42;
}
