// CCCC_FLAGS: --build
//
// DirExists: 1 if path exists and is a directory, 0 otherwise.

[[cccc::build]]
int build_main(Builder *ctx) {
    // /tmp is a directory on both macOS and Linux.
    if (!DirExists(ctx, "/tmp")) return 1;

    // The build root itself is a directory.
    if (!DirExists(ctx, BuildRoot(ctx))) return 1;

    // A path that definitely does not exist.
    if (DirExists(ctx, "/this/does/not/exist/cccc_xyz999")) return 1;

    // FileExists returns 1 for directories too — DirExists is more specific.
    // /tmp is a dir, so both should agree.
    if (!FileExists(ctx, "/tmp")) return 1;

    return 42;
}
