// JCC_FLAGS: --std=c99
int sum(int n) {
    int arr[n];
    for (int i = 0; i < n; i++) arr[i] = i * 10;
    return arr[2];
}
int main(void) { return sum(3) == 20 ? 42 : 1; }
