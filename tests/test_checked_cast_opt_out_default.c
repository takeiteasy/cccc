// #486: the checked-cast desugar/rewrite is gated on --checked-pointers,
// same as #919/#944's own rewrites -- without the flag, an annotated cast
// stays a plain cast (no compiler-generated temp, no CHKR/CHKAB anywhere),
// so an out-of-declared-bounds access made through it is not caught. Reads
// one element past the claimed count(5) (a mild, adjacent-stack overread,
// not a wild one) to prove no check fires, without relying on UB severe
// enough to crash outright.

int main(void) {
    int  arr[5] = {1, 2, 3, 4, 5};
    int *raw    = arr;
    int  n      = 5;
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::assume]])raw;
    volatile int i = 5;
    int          x = p[i];
    (void)x;
    return 42;
}
