// CCCC_FLAGS: --build
//
// #569: CopyFile, MoveFile, DeleteFile, MkDir, DeleteDir build API helpers.

#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    // Use a unique temp directory to avoid collisions.
    const char *tmpdir = "/tmp/cccc_test_fsops";

    // Clean up in case of a previous run.
    DeleteDir(ctx, tmpdir);

    // MkDir creates a directory tree.
    if (MkDir(ctx, "/tmp/cccc_test_fsops/subdir") != 0) return 1;
    if (!DirExists(ctx, "/tmp/cccc_test_fsops/subdir")) return 1;

    // WriteFile creates a file we can copy.
    const char *src = "/tmp/cccc_test_fsops/src.txt";
    if (WriteFile(ctx, src, "hello cccc\n") != 0) return 1;
    if (!FileExists(ctx, src)) return 1;

    // CopyFile.
    const char *copy = "/tmp/cccc_test_fsops/copy.txt";
    if (CopyFile(ctx, src, copy) != 0) return 1;
    if (!FileExists(ctx, copy)) return 1;
    const char *contents = ReadFile(ctx, copy);
    if (!contents || strcmp(contents, "hello cccc\n") != 0) return 1;

    // MoveFile (same-device rename).
    const char *moved = "/tmp/cccc_test_fsops/moved.txt";
    if (MoveFile(ctx, copy, moved) != 0) return 1;
    if (!FileExists(ctx, moved)) return 1;
    if (FileExists(ctx, copy)) return 1; // old path must be gone

    // DeleteFile.
    if (DeleteFile(ctx, moved) != 0) return 1;
    if (FileExists(ctx, moved)) return 1;

    // DeleteDir removes the whole tree.
    if (DeleteDir(ctx, tmpdir) != 0) return 1;
    if (DirExists(ctx, tmpdir)) return 1;

    return 42;
}
