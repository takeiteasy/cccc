#include <stdio.h>

#ifndef BENCH_N
#define BENCH_N 10
#endif

static long total = 0;

static void solve(int n, int *cols, int row) {
    if (row == n) {
        total++;
        return;
    }
    for (int c = 0; c < n; c++) {
        int ok = 1;
        for (int r = 0; r < row; r++) {
            int d = cols[r] - c;
            if (d < 0)
                d = -d;
            if (cols[r] == c || d == row - r) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            cols[row] = c;
            solve(n, cols, row + 1);
        }
    }
}

int main(void) {
    int cols[BENCH_N];
    solve(BENCH_N, cols, 0);
    printf("result: %ld\n", total);
    return 42;
}
