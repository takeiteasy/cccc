// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not forwarded into comptime
// Ticket #887: an object-like #define from the runtime translation unit is
// not visible inside a comptime function body (isolate_comptime_macros
// strips source-file #defines before the comptime preprocess/parse). This
// pins the pointed diagnostic added for that specific confusion -- the
// reporter expected ordinary preprocessing to have already substituted the
// macro before comptime ever saw it. See @shared / -D / --comptime-include-all
// for the supported ways to make a #define visible inside a comptime body.

#define MY_STR "hello"

[[cccc::comptime]]
int helper(const char *s) {
    return (int)s[0];
}

[[cccc::comptime]]
Node *gen(void) {
    int r = helper(MY_STR);
    return MakeIntLiteral(r);
}

int result = gen();

int main(void) {
    return result;
}
