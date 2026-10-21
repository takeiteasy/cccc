// -c=native, atomic load/store/exchange/CAS run natively -- an atomic store in
// statement position used to serialize to a null statement and silently vanish
int main(void) {
    int x = 0;
    __builtin_atomic_store(&x, 20);
    int y        = __builtin_atomic_load(&x);
    int old      = __builtin_atomic_exchange(&x, 7);
    int expected = 7;
    int ok       = __builtin_compare_and_swap(&x, &expected, 15);
    return y + old + x + (ok ? 0 : 100) - 13;
}
