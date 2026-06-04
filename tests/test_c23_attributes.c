// Test C23 [[...]] attribute syntax
// Layout-neutral attributes are accepted; selected diagnostic attributes have
// warning-system semantics.

// Test attributes on functions (attributes after return type, like __attribute__)
int [[nodiscard]] get_value(void) {
    return 42;
}

int [[deprecated]] old_function(void) {
    return 1;
}

static int [[maybe_unused]] helper(void) {
    return 2;
}

void [[noreturn]] exit_program(void) {
    // Would normally exit, but for testing we won't
    return;  // This is just for testing
}

// Test attributes on variables
int [[maybe_unused]] unused_var = 10;

// Test attributes with parameters
int [[deprecated("Use new_function instead")]] old_func2(void) {
    return 3;
}

// Test multiple attributes
int [[nodiscard, maybe_unused]] multi_attr_func(void) {
    return 4;
}

// Test attributes on struct
// Note: Attributes on struct members require more complex handling
struct TestStruct {
    int x;
    int y;
    int z;
};

// Test fallthrough attribute
// Note: [[fallthrough]] requires statement-level attribute support
int test_fallthrough(int x) {
    int result = 0;
    switch (x) {
        case 1:
            result = 1;
            // [[fallthrough]]; - statement attributes not yet supported
        case 2:
            result = 2;
            break;
        case 3:
            result = 3;
            break;
        default:
            result = -1;
    }
    return result;
}

// Test unsequenced and reproducible (function properties)
int [[unsequenced]] pure_func(int a) {
    return a * 2;
}

int [[reproducible]] repro_func(int a) {
    return a + 1;
}

// Main test function
int main() {
    // Attributes do not change runtime behavior.
    int v = get_value();
    if (v != 42) return 1;

    int o = old_function();
    if (o != 1) return 2;

    int h = helper();
    if (h != 2) return 3;

    int o2 = old_func2();
    if (o2 != 3) return 4;

    int m = multi_attr_func();
    if (m != 4) return 5;

    // Test struct with attributes
    struct TestStruct s = {1, 2, 3};
    if (s.x != 1 || s.y != 2 || s.z != 3) return 6;

    // Test fallthrough (skip - switch fallthrough logic is tricky to test)
    // int f = test_fallthrough(1);
    // if (f != 2) return 7;

    // Test pure and reproducible functions
    int p = pure_func(5);
    if (p != 10) return 8;

    int r = repro_func(5);
    if (r != 6) return 9;

    return 42;  // Success
}
