// #1300: found via #1132's round-13 self-hosting spike (src/stdlib/pthread.c's
// own wrap_pthread_once(): `(int *)((char *)once_control + offsetof(pthread_
// once_t, __opaque))`, `once_control` a `long long` parameter). serialize_
// expr.c's strip_casts() (peeling past usual_arith_conv()'s own bogus
// pointer-typed wrap around a scaled integer offset, so ND_ADD's pointer-
// arithmetic case can tell which side is really the pointer) used an
// unbounded `while` loop -- past usual_arith_conv()'s own single wrapping
// cast it kept going, peeling straight through a second, genuinely
// meaningful cast the guest source itself wrote (`(char *)an_integer_
// variable`, an explicit integer-to-pointer conversion), revealing the
// pre-cast *integer* operand underneath and misclassifying a genuinely
// pointer-typed operand position as integer-ish. Neither of ND_ADD's two
// pointer-arithmetic arms then matched, so it fell through to plain
// arithmetic and printed the RHS's own still-bogus pointer-typed cast
// unstripped: `(char *)an_int + (char *)(offset)` -- pointer + pointer,
// invalid in C. Fixed by peeling exactly one cast layer (an `if`, not a
// `while`) -- usual_arith_conv()'s own wrap is applied exactly once.
// Asserts VM 42 -> native 42; the bug is a host compile failure ("invalid
// operands to binary expression") no -m shape assertion alone can see.
#include <stddef.h>

typedef struct {
    long __sig;
    char __opaque[8];
} ptr_int_cast_1300_t;

static int compute(long long base) {
    int *slot = (int *)((char *)base + offsetof(ptr_int_cast_1300_t, __opaque));
    return *slot;
}

int main(void) {
    ptr_int_cast_1300_t v = {0, {42, 0, 0, 0, 0, 0, 0, 0}};
    return compute((long long)&v);
}
