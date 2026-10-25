// Without --checked-pointers the per-dimension checks emit nothing: a row
// overflow that stays inside the object runs clean (a read, as in
// test_checked_array_opt_out_default.c).
int main(void) {
    int a        _Checked[3][5] = {{0}};
    volatile int j              = 5;
    int          x              = a[0][j];
    (void)x;
    return 42;
}
