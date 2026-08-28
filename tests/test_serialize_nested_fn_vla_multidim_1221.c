// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -m
// CCCC_EXPECT_STDERR: multi-dimensional variable-length-array local
//
// #1221 (follow-up to #1209): a fully multi-dimensional VLA -- every
// extent runtime-sized, `int v[n][m]` -- read by a nested function still
// can't be captured across a static link the way #1209 fixed a 1-D VLA (or
// a VLA with a fixed inner extent, `int v[n][3]`). The env-struct field
// would need to be `int (*)[][m]`, a pointer to an array of incomplete
// element type -- illegal C, since an array's element type must itself be
// complete. record_nested_upvar() (src/serialize_program.c) rejects this
// shape with a diagnostic naming it explicitly rather than emitting broken
// C.
static int outer(void) {
    int n = 3, m = 4;
    int v[n][m];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            v[i][j] = i * m + j;
    int sum(void) {
        int s = 0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < m; j++)
                s += v[i][j];
        return s;
    }
    return sum();
}

int main(void) {
    return outer() - 66 + 42;
}
