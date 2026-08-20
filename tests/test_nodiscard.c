// CCCC_FLAGS: --std=c23 -Wnodiscard
// Test [[nodiscard]] attribute
// Expected return: 42

int [[nodiscard]] get_value(void) {
    return 42;
}

int [[nodiscard("Use get_value instead")]] old_get_value(void) {
    return 0;
}

typedef struct [[nodiscard]] {
    int x;
} NodiscardStruct;

NodiscardStruct make_ns(void) {
    NodiscardStruct ns = {1};
    return ns;
}

int test_nodiscard_func(void) {
    return get_value();
}

int test_nodiscard_void_cast(void) {
    (void)get_value();
    return 42;
}

int main(void) {
    int v = get_value();
    if (v != 42)
        return 1;

    if (test_nodiscard_func() != 42)
        return 2;

    if (test_nodiscard_void_cast() != 42)
        return 3;

    return 42;
}