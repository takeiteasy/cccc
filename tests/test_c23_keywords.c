// Test C23 keywords: static_assert and typeof_unqual

// Test static_assert (C23 keyword alias for _Static_assert)
static_assert(1, "static_assert should work");
static_assert(sizeof(int) >= 4, "int size check");
static_assert(sizeof(char) == 1);

// Test typeof_unqual (removes qualifiers)
int main() {
    // Test static_assert in function scope
    static_assert(1 + 1 == 2, "basic math");
    static_assert(10 > 5, "comparison");
    static_assert(sizeof(long) == 8);

    // Test typeof_unqual removes const
    const int x = 42;
    typeof_unqual(x) y = 100;  // y should be non-const int
    y = 200;  // This should work since y is not const

    // Test typeof_unqual with volatile
    volatile int v = 10;
    typeof_unqual(v) w = 20;  // w should be non-volatile int
    w = 30;

    // Test typeof_unqual with const pointer
    const int *cp = &x;
    typeof_unqual(*cp) z = 50;  // z should be non-const int
    z = 60;

    // Test that regular typeof still works
    typeof(x) a = 77;  // a should be const int (const is preserved)

    // Test typeof_unqual with type names
    typeof_unqual(const int) b = 88;
    b = 99;

    // Verify values
    if (y != 200) return 1;
    if (w != 30) return 2;
    if (z != 60) return 3;
    if (b != 99) return 4;

    return 42;  // Success
}
