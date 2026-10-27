// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A callback parameter annotated only in the prototype is checked at calls
// made through it inside the definition.

void apply(void (*cb)(int *[[cccc::array, cccc::count(n)]] p, int n));

void apply(void (*cb)(int *p, int n)) {
    int *[[cccc::array, cccc::count(2)]] a = (int[2]){1, 2};
    cb(a, 8);
}

static void ignore(int *p, int n) {
    (void)p;
    (void)n;
}

int main(void) {
    apply(ignore);
    return 42;
}
