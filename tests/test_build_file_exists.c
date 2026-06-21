// CCCC_FLAGS: --build
//
// FileExists: returns 1 if a path exists (file, directory, symlink, etc.),
// 0 otherwise.

[[cccc::build]]
int build_main(Builder *ctx) {
    // A path that definitely doesn't exist
    if (FileExists(ctx, "/this/path/does/not/exist/xyz_cccc_test")) return 1;

    // /tmp exists on both macOS and Linux
    if (!FileExists(ctx, "/tmp")) return 1;

    // The build root itself exists
    if (!FileExists(ctx, BuildRoot(ctx))) return 1;

    return 42;
}
