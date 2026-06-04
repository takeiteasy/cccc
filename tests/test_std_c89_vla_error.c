/* JCC_FLAGS: -std=c89 -Wpedantic */
/* JCC_EXPECT_STDERR: warning: variable-length arrays are a C99 extension \[-Wpedantic\] */
int f(int n) {
    int arr[n];
    arr[0] = 42;
    return arr[0];
}

int main(void) {
    return f(1);
}
