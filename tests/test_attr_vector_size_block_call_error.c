// EXPECT_COMPILE_ERROR
// Vector-by-value through a GNU/Apple block invocation (ND_BLOCK_CALL) is
// not supported (tracker #714 follow-up): block calls have no by-memory
// (RETBUF/pointer-arg) ABI for aggregates at all. Must be rejected with a
// clear diagnostic rather than mis-marshalling a vregs[]-resident value
// through a plain integer argument register.

typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    int (^blk)(v4sf) = ^(v4sf v) { return (int)v[0]; };
    v4sf a;
    a[0] = 1.0f;
    return blk(a) == 1 ? 42 : 1;
}
