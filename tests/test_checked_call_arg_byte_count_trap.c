// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #488: caller-side verification, byte_count(n) form -- same mechanism as
// count(n) (test_checked_call_arg_count_trap.c) but the parameter's bound
// is a byte extent, not an element count. `small` is byte_count(2); the
// call claims byte_count(100).

void sink(char *[[cccc::array, cccc::byte_count(n)]] p, int n);

int main(void) {
    char *[[cccc::array, cccc::byte_count(2)]] small = (char[2]){1, 2};
    sink(small, 100);
    return 42;
}

void sink(char *p, int n) {
    (void)p;
    (void)n;
}
