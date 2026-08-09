// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: prepost
// CCCC_EXPECT_STDOUT: \$UNSET_CCCC_TEST_VAR_XYZ
// CCCC_REJECT_STDOUT: pre\$UNSET_CCCC_TEST_VAR_XYZpost
//
// RunCustom's vendored shell now expands $VAR / ${VAR} in unquoted and
// double-quoted words (single-quoted words stay fully literal, matching
// POSIX). An unset variable expands to nothing rather than being left as
// the literal text -- confirmed with a name unlikely to ever be set in a
// test environment.

[[cccc::build]]
int build_main(Builder *ctx) {
    // Unset var expands to empty: "pre" + "" + "post" == "prepost".
    RunCustom(ctx, "unset_expand", "echo pre${UNSET_CCCC_TEST_VAR_XYZ}post");
    // Single-quoted: no expansion, the literal "$NAME" text passes through.
    RunCustom(ctx, "quoted_literal", "echo '$UNSET_CCCC_TEST_VAR_XYZ'");
    return BuildDefault(ctx);
}
