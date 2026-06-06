// JCC_FLAGS: --std=c23
// Test constexpr parsing in JCC
constexpr int MAX_SIZE = 100;

constexpr int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

int main() {
    return 42;
}
