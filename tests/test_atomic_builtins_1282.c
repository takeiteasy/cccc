// Expected return: 42
// #1282: GCC/Clang __atomic_* builtin family frontend support (added so
// src/stdlib/pthread.c's __atomic_compare_exchange_n calls could self-host,
// self-hosting spike #1132) -- the _n forms alias the existing ND_ALOAD/
// ND_ASTORE/ND_EXCH/ND_CAS/ND_FENCE nodes reached today only through
// <stdatomic.h>'s __builtin_* spellings; the non-_n forms desugar to those;
// __atomic_fetch_*/__atomic_*_fetch are a CAS retry loop (including nand,
// which has no direct NodeKind); test_and_set/clear route through
// exchange/store; the lock-free predicates and both fences round out the
// family. Every memory-order argument is accepted and discarded (every
// operation here is unconditionally seq_cst).
int main(void) {
    int x   = 5;
    int old = __atomic_load_n(&x, __ATOMIC_SEQ_CST);
    if (old != 5)
        return 1;
    __atomic_store_n(&x, 10, __ATOMIC_SEQ_CST);
    if (x != 10)
        return 2;
    int prev = __atomic_exchange_n(&x, 20, __ATOMIC_SEQ_CST);
    if (prev != 10 || x != 20)
        return 3;
    int expected = 20;
    int ok = __atomic_compare_exchange_n(&x, &expected, 30, 0, __ATOMIC_SEQ_CST,
                                         __ATOMIC_SEQ_CST);
    if (!ok || x != 30)
        return 4;
    ok = __atomic_compare_exchange_n(&x, &expected, 99, 0, __ATOMIC_SEQ_CST,
                                     __ATOMIC_SEQ_CST);
    if (ok || expected != 30)
        return 5;

    int y = 1;
    int ret;
    __atomic_load(&y, &ret, __ATOMIC_SEQ_CST);
    if (ret != 1)
        return 6;
    int newval = 7;
    __atomic_store(&y, &newval, __ATOMIC_SEQ_CST);
    if (y != 7)
        return 7;
    int swapval = 8, retval;
    __atomic_exchange(&y, &swapval, &retval, __ATOMIC_SEQ_CST);
    if (retval != 7 || y != 8)
        return 8;
    int expected2 = 8, desired2 = 9;
    ok = __atomic_compare_exchange(&y, &expected2, &desired2, 0,
                                   __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (!ok || y != 9)
        return 9;

    int z  = 100;
    int fo = __atomic_fetch_add(&z, 5, __ATOMIC_SEQ_CST);
    if (fo != 100 || z != 105)
        return 10;
    int af = __atomic_add_fetch(&z, 5, __ATOMIC_SEQ_CST);
    if (af != 110 || z != 110)
        return 11;
    __atomic_fetch_sub(&z, 10, __ATOMIC_SEQ_CST);
    if (z != 100)
        return 12;
    __atomic_fetch_and(&z, 0xF0, __ATOMIC_SEQ_CST);
    if (z != (100 & 0xF0))
        return 13;
    z = 5;
    __atomic_fetch_or(&z, 2, __ATOMIC_SEQ_CST);
    if (z != 7)
        return 14;
    __atomic_fetch_xor(&z, 3, __ATOMIC_SEQ_CST);
    if (z != 4)
        return 15;
    unsigned int n   = 0xF0F0;
    unsigned int nfo = __atomic_fetch_nand(&n, 0xFF00, __ATOMIC_SEQ_CST);
    if (nfo != 0xF0F0)
        return 16;
    if (n != (unsigned int)(~(0xF0F0u & 0xFF00u)))
        return 17;

    int flag     = 0;
    int prevflag = __atomic_test_and_set(&flag, __ATOMIC_SEQ_CST);
    if (prevflag != 0 || flag != 1)
        return 18;
    __atomic_clear(&flag, __ATOMIC_SEQ_CST);
    if (flag != 0)
        return 19;

    if (!__atomic_is_lock_free(4, &x))
        return 20;
    if (!__atomic_always_lock_free(8, 0))
        return 21;
    if (__atomic_is_lock_free(3, &x))
        return 22;

    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_signal_fence(__ATOMIC_ACQUIRE);

    return 42;
}
