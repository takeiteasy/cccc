// JCC_FLAGS: -std=c17
int [[nodiscard]] f(void) { return 42; }
int main(void) { return f(); }
