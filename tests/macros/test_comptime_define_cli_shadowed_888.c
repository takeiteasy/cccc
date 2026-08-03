// CCCC_FLAGS: -DN=8
// Ticket #888 (found while investigating): a -D value used to be silently
// defeated when the source also defined the same name -- the source #define
// overwrites the hashmap entry (non-NULL define_tok), isolate_comptime_macros
// strips it, and the -D value never reaches the comptime pass. That's exactly
// the workaround the #887 diagnostic recommends, silently failing here.

#define N 8

[[cccc::comptime]]
Node *gen(void) {
    int floats[N];
    return MakeIntLiteral((int)(sizeof(floats) / sizeof(int)));
}
int result = gen();

int main(void) {
    return result == 8 ? 42 : 1;
}
