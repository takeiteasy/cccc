// CCCC_FLAGS: --testing --checked-pointers
// A product in a bounds template that wraps traps at the call instead of
// reading as a small bound that lets an oversized call through.

#include <stdio.h>
#include <stddef.h>

static int sum(int *[[cccc::array, cccc::count(n)]] p, size_t n) {
    return n ? p[0] : 0;
}

static int rect(int *[[cccc::array, cccc::count(w * h)]] p, size_t w,
                size_t h) {
    return w * h ? p[0] : 0;
}

[[cccc::test]]
void test_products_in_bounds(void) {
    int    a[6] = {42};
    FILE  *f    = tmpfile();
    size_t two  = 2;
    AssertEq(sum(a, 6), 42);
    AssertEq(rect(a, 2, 3), 42);
    AssertEq(rect(a, 0, 3), 0);
    AssertEq(fwrite(a, two, 2, f), 2);
    fclose(f);
}

// 2^63 * 2 wraps to 0.
[[cccc::test(exit_code = 255)]]
void test_fread_product_wraps(void) {
    char            a[8];
    FILE           *f    = tmpfile();
    volatile size_t size = (size_t)1 << 63;
    fread(a, size, 2, f);
}

[[cccc::test(exit_code = 255)]]
void test_fwrite_product_wraps(void) {
    char            a[8] = {0};
    FILE           *f    = tmpfile();
    volatile size_t size = (size_t)1 << 63;
    fwrite(a, size, 2, f);
}

// (2^62 + 1) * sizeof(int) wraps to 4.
[[cccc::test(exit_code = 255)]]
void test_count_times_element_size_wraps(void) {
    int             a[1] = {0};
    volatile size_t n    = ((size_t)1 << 62) + 1;
    sum(a, n);
}

[[cccc::test(exit_code = 255)]]
void test_user_product_wraps(void) {
    int             a[1] = {0};
    volatile size_t w    = (size_t)1 << 32;
    volatile size_t h    = (size_t)1 << 32;
    rect(a, w, h);
}
