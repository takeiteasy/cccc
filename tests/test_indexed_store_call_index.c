// Regression: indexed loads/stores at -O2+ go through the fused
// LDR_INDEX/STR_INDEX opcodes. Two distinct codegen bugs lived here (#581):
//
//   1. An *unsigned* post-increment index `buf[n++]` lowers to
//      `(unsigned)((n += 1) - 1)`, which evaluates to `1 + 0xFFFFFFFF ==
//      0x100000000` in a 64-bit register. match_indexed_addr strips the
//      widening cast that would have truncated it, so without an explicit
//      re-truncation the index carried a stray bit-32 and addressed ~4 GiB out
//      of bounds -> SIGSEGV.
//
//   2. When the index expression contains a call, the fused opcode held a base
//      address in a caller-saved temp across the call, which clobbered it.
//
// All paths must agree with the unfused -O0/-O1 result.

unsigned ucount(void) { return 3; }

int main(void) {
    char buf[64];
    for (int i = 0; i < 64; i++) buf[i] = 0;

    // (1) unsigned post-increment index (fused store)
    unsigned n = 0;
    for (int c = 'A'; c <= 'E'; c++)
        buf[n++] = (char)c;
    if (n != 5) return 1;
    if (buf[0] != 'A' || buf[1] != 'B' || buf[2] != 'C' ||
        buf[3] != 'D' || buf[4] != 'E')
        return 2;

    // unsigned post-increment index (fused load)
    unsigned r = 0;
    int acc = 0;
    while (r < 5)
        acc += buf[r++];
    if (acc != 'A' + 'B' + 'C' + 'D' + 'E') return 3;

    // (2) index expression containing a call
    buf[ucount()] = 'Z';
    if (buf[3] != 'Z') return 4;

    return 42;
}
