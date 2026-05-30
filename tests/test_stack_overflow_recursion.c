// EXPECT_RUNTIME_ERROR - Test stack overflow detection with deep recursion

int recurse(int n) {
    if (n <= 0) return 0;
    return recurse(n - 1) + 1;
}

int main() {
    return recurse(100000);
}
