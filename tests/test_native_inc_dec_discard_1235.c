// -c=native regression (#1235): a postfix `i++`/`i--` in a value-discarding
// position must not emit the dead value-reconstruction term that trips
// -Wunused-value.
//
// new_inc_dec() lowers `A++` to `(typeof A)((A += 1) + -1)`; the trailing
// `+ -1` reconstructs the pre-increment value the expression must yield. In
// a for-loop update clause or a bare `i++;` statement that value is never
// read, so clang/gcc warn `expression result unused`.
// serialize_discard_expr() now peels the cast and the compensating term at
// those two sites, emitting just the underlying
// `(tmp = &A, *tmp = *tmp + 1)` store. A value-position use (`j = i++;`)
// still keeps the term.

int main(void) {
    int    arr[4] = {0, 0, 0, 0};
    int    i;
    int   *p;
    double d;

    // for-update clause: plain local, pointer, and float post-increment.
    for (i = 0; i < 4; i++)
        arr[i] = i + 1; // arr -> {1, 2, 3, 4}
    for (p = arr; p < arr + 4; p++)
        *p += 10;       // arr -> {11, 12, 13, 14}
    for (d = 0.0; d < 3.0; d++)
        ;

    // bare expression statements (also a discard context).
    i--; // 3
    p--; // &arr[3]
    d--; // 2.0

    // value position: the compensating term must survive here.
    int j    = i++;                               // j = 3, i = 4

    int sum  = arr[0] + arr[1] + arr[2] + arr[3]; // 50
    int here = (int)(p - arr);                    // 3
    return sum + i + j + (int)d - here - 14; // 50 + 4 + 3 + 2 - 3 - 14 = 42
}
