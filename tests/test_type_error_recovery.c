// EXPECT_COMPILE_ERROR
// Test type error recovery
// Should report type errors and continue

int main() {
    int arr[5];
    int *ptr;

    // Error: cannot assign to array
    arr = ptr;

    // Error: invalid pointer dereference (dereferencing non-pointer)
    int x = *5;

    return 42;
}
