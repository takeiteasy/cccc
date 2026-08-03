// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'UNDEF_S'
// Ticket #887: an undefined identifier used in a pointer context inside a
// comptime function body (as a bare initializer, or as an argument to
// another comptime function) must produce a clean compile error, not
// execute the resulting ty_error-laden bytecode and crash the host.

[[cccc::comptime]]
int helper(const char *s) { return (int)s[0]; }

[[cccc::comptime]]
Node *gen(void) {
    int r = helper(UNDEF_S);
    return MakeIntLiteral(r);
}

int result = gen();

int main(void) { return result; }
