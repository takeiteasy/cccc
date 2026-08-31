// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: quote placeholder '\$1' in statement position must be followed by ';'
// Ticket #1242: a bare `$1` in statement-list position (no trailing `;`) is
// not valid C -- `{ x }` isn't either -- but the generic "expected ';'" from
// skip() didn't say what a Quote() caller should actually write. expr_stmt()
// (src/parse_stmt.c) now names the fix when the unterminated expression was
// a quote placeholder.

[[cccc::comptime]]
Node *bad(void) {
    Node *body = Quote("(void)0;");
    return Quote("for (int i = 0; i < 3; i++) { $1 }", body);
}

int main(void) {
    bad();
    return 42;
}
