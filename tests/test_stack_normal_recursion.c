// Test normal recursion without stack overflow

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main() {
    int result = factorial(10);
    return result == 3628800 ? 42 : 1;
}
