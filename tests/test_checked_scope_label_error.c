// EXPECT_COMPILE_ERROR
// Checked regions (#485): 'checked'/'unchecked' is not allowed on a label --
// label_attr is reused for the label-body attributes in stmt()
// (src/parse_stmt.c), and nothing ever consumes checked_scope for a bare
// ND_LABEL node, so this is rejected explicitly rather than silently
// dropped.

int main(void) {
[[cccc::checked]] done:
    return 42;
}
