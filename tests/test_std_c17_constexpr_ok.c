// JCC_FLAGS: -std=c17
// constexpr is a valid identifier in C17 (downgraded from keyword)
int constexpr = 42;
int main(void) { return constexpr == 42 ? 42 : 1; }
