// CCCC_FLAGS: --build
//
// GlobFiles: expand a glob pattern and return a NULL-terminated array of paths.

[[cccc::build]]
int build_main(Builder *ctx) {
    // tests/ contains many .c files — glob should return at least one.
    const char **matches = GlobFiles(ctx, "tests/*.c");
    if (!matches) return 1;

    int count = 0;
    while (matches[count]) count++;
    if (count < 1) return 1;

    // Each returned path must be non-empty.
    for (int i = 0; i < count; i++)
        if (!matches[i] || matches[i][0] == '\0') return 1;

    // A pattern that matches nothing returns NULL.
    const char **none = GlobFiles(ctx, "tests/no_match_cccc_xyz_*.zzz");
    if (none != (void*)0) return 1;

    return 42;
}
