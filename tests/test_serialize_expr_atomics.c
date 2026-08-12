// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __atomic_compare_exchange_n\(&x, &expected, 15, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST\)
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// The four atomic node kinds map onto the host `__atomic_*` builtins with
// the same contract the VM's opcodes have: compare-and-swap takes a
// *pointer* to the expected value and yields a bool, exchange yields the old
// value, and both reject anything but a 1/2/4/8-byte non-float pointee.
//
// An atomic store is the case that made this class dangerous: it sits in
// statement position, where the serializer's fallback emitted `/* ... */;` —
// a valid null statement — so the program built and silently skipped the
// store. It serializes to a plain `__atomic_store_n(...)` call here; only in
// genuine expression position does it need the value-preserving form, since
// `__atomic_store_n` returns void but the VM gives it C assignment
// semantics.

int main(void) {
    int x = 0;
    __builtin_atomic_store(&x, 20);
    int y = __builtin_atomic_load(&x);
    int old = __builtin_atomic_exchange(&x, 7);
    int expected = 7;
    int ok = __builtin_compare_and_swap(&x, &expected, 15);
    return y + old + x + (ok ? 0 : 100) - 13;
}
