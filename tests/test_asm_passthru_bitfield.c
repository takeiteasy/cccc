// CCCC_FLAGS: --asm-passthru
//
// A bit-field asm operand goes through a temporary: seeded for "+", passed by
// address, then stored back into the bit-field once the asm has run.

#if defined(__aarch64__)
#define MOVW "mov %w0, %w1"
#define ADDI "add %w0, %w0, %1"
#elif defined(__x86_64__)
#define MOVW "movl %1, %0"
#define ADDI "addl %1, %0"
#endif

struct S {
    int      lo : 3;
    unsigned mid : 5;
    int      hi : 4;
};

static int calls;
static struct S *next(struct S *p) {
    calls++;
    return p;
}

int main(void) {
    struct S s = {.lo = 1, .mid = 9, .hi = -2};

    // "+r" on a signed field leaves its neighbours alone.
    __asm__(ADDI : "+r"(s.lo) : "i"(2));
    if (s.lo != 3 || s.mid != 9 || s.hi != -2)
        return 1;

    // The result wraps to the field's width.
    __asm__(ADDI : "+r"(s.lo) : "i"(1));
    if (s.lo != -4 || s.mid != 9 || s.hi != -2)
        return 2;

    // "=r" on an unsigned field at a non-zero offset truncates a wider value.
    int wide = 0x1F5;
    __asm__(MOVW : "=r"(s.mid) : "r"(wide));
    if (s.mid != 0x15 || s.lo != -4 || s.hi != -2)
        return 3;

    // Negative values round-trip.
    int neg = -5;
    __asm__(MOVW : "=r"(s.hi) : "r"(neg));
    if (s.hi != -5 || s.mid != 0x15 || s.lo != -4)
        return 4;

    // A bit-field input is read as a value.
    int out = 0;
    __asm__(MOVW : "=r"(out) : "r"(s.hi));
    if (out != -5)
        return 5;

    // The lvalue is evaluated once.
    __asm__(ADDI : "+r"(next(&s)->mid) : "i"(1));
    if (calls != 1 || s.mid != 0x16)
        return 6;

    return 42;
}
