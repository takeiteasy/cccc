// JCC_FLAGS: -std=c99
int sum(int n, int * restrict a, int * restrict b) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i] + b[i];
    return s;
}
int main(void) { return 42; }
