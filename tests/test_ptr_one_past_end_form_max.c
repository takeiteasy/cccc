// CCCC_FLAGS: -3
// #983: same as test_ptr_one_past_end_form.c but at -3, which additionally
// enables CHKPA (provenance check on the arithmetic result). CHKPA already
// permitted a one-past-the-end result (`ptr > end` is its own error
// condition, not `>=`) even before #983 -- this pins that down as a
// regression test alongside the CHKB relaxation, since both checks run on
// the same `p + 4` expression under -3.
#include <stdlib.h>
int main(void) {
    int *p = malloc(4 * sizeof(int));
    if (!p)
        return 255;
    int *e = p + 4;   // legal to form -- must not trap under -3 either
    long d = e - p;
    free(p);
    return d == 4 ? 42 : 1;
}
