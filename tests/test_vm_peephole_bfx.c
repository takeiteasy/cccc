// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : UBFX +[0-9]+
// CCCC_EXPECT_STDOUT: : SBFX +[0-9]+
// CCCC_EXPECT_STDOUT: : BFI +[0-9]+
// CCCC_REJECT_STDOUT: : (USHRI3|SHRI3|SHLI3|ANDI3)
// A shift followed by a mask, a shift pair, and a bit-field read or write are
// one extract or insert opcode each.
typedef struct {
    unsigned a : 3;
    int      b : 5;
    unsigned c : 24;
} S;

unsigned long ubfx(unsigned long x) {
    return (x >> 8) & 0xff;
}

long sbfx(long x) {
    return (x << 44) >> 52;
}

int get_b(const S *s) {
    return s->b;
}

unsigned get_c(const S *s) {
    return s->c;
}

void set_b(S *s, int v) {
    s->b = v;
}

int main(void) {
    S s = {0};
    set_b(&s, -3);
    s.c = 0xabcdef;
    if (get_b(&s) != -3 || get_c(&s) != 0xabcdef)
        return 1;
    if (ubfx(0x1234567890abcdefUL) != 0xcd || sbfx(0xff000) != -16)
        return 2;
    return 42;
}
