// CCCC_FLAGS: -3 -d
// CCCC_EXPECT_STDOUT: STKTAG
// An array is still recorded when it escapes some other way, and a call in an
// element's index expression is still scanned for escapes.
int sum(int *p) {
    return p[0];
}

int idx(int *p) {
    return *p & 1;
}

int main(void) {
    int arr[4] = {0};
    int x      = 0;
    arr[idx(&x)]++;
    arr[1] += 2;
    return sum(arr) + 42 == 42 && arr[1] == 2 ? 42 : 1;
}
