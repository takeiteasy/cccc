// CCCC_FLAGS: --build --build-dry-run --deps-file=build/deps_file_reject.d
// CCCC_EXPECT_STDERR: --deps-file requires -c=native
//
// #1308: --deps-file only makes sense for a mode that opens the full source
// + header set and then exits without running the VM (-c=native / -m /
// -c=generated). In any other mode — here --build — it must hard-error
// rather than be silently ignored.

[[cccc::build]]
int build_main(Builder *ctx) {
    return BuildDefault(ctx);
}
