// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __atomic_store_n\(\(int \*\)&x, 7.*__atomic_load_n\(\(int \*\)&x.*__atomic_compare_exchange_n\(\(int \*\)&x, &e.*__atomic_store_n\(\(long \*\)&l.*__atomic_exchange_n\(\(_Bool \*\)&f, 1
// CCCC_REJECT_STDOUT: __atomic_[a-z_]+_n\(&(x|e|l|f)
//
// #1101: clang rejects an &_Atomic-qualified lvalue handed straight to the
// host __atomic_* builtins ("address argument to atomic operation must be a
// pointer to integer or pointer"), because declarations serialize through
// the host typedef names (atomic_int -> real <stdatomic.h> _Atomic int) and
// so &x has type _Atomic(int)*. The serializer now casts every address
// operand to its pointee with the qualifier stripped -- (int *)&x,
// (long *)&l, (_Bool *)&f for atomic_flag -- leaving the object itself
// genuinely _Atomic in the emitted C. The REJECT guard fails the test if
// any __atomic_* call ever regresses to a bare &var first argument.

#include <stdatomic.h>

int main(void) {
    atomic_int x = 0;
    __builtin_atomic_store(&x, 7);
    int y        = __builtin_atomic_load(&x);
    int e        = 7;
    int r        = __builtin_compare_and_swap(&x, &e, 15);
    atomic_long l = 0;
    __builtin_atomic_store(&l, 100LL);
    long ly     = __builtin_atomic_load(&l);
    atomic_flag f = ATOMIC_FLAG_INIT(0);
    __builtin_atomic_exchange(&f, 1);
    return y + r + (int)ly + f ^ x ^ 41;
}
