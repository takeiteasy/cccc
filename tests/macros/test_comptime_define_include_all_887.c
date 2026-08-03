// CCCC_FLAGS: --comptime-include-all
// Ticket #887: --comptime-include-all is one of the documented ways to make
// a runtime-TU #define visible inside a comptime function body (the others
// are @shared header routing and -D). This is the positive counterpart to
// test_comptime_define_note_887.c's negative case.

#define MY_STR "hello"

[[cccc::comptime]]
int helper(const char *s) { return (int)s[0]; }

[[cccc::comptime]]
Node *gen(void) {
    int r = helper(MY_STR);
    return MakeIntLiteral(r);
}

int result = gen();

int main(void) { return result == 'h' ? 42 : 1; }
