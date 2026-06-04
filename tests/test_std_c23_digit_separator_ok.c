// JCC_FLAGS: -std=c23
int main(void) {
    int n = 1'000'000;
    return n == 1000000 ? 42 : 1;
}
