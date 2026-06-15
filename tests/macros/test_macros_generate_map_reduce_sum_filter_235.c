// Ticket #235: $generate_sum/$generate_map/$generate_reduce/$generate_filter
// FP-style array generators for a scalar element type (int).

#include <stdbool.h>
#include <stddef.h>

[[cccc::comptime]]
void setup_fp_helpers(void) {
    $type_t *int_ty = $get_type("int");
    $generate_sum(int_ty);
    $generate_map(int_ty);
    $generate_reduce(int_ty);
    $generate_filter(int_ty);
}

setup_fp_helpers();

static int double_it(int x) { return x * 2; }
static int add(int a, int b) { return a + b; }
static bool is_even(int x) { return x % 2 == 0; }

int main(void) {
    int arr[] = {1, 2, 3, 4, 5};
    size_t n = 5;

    // sum_int: 1+2+3+4+5 = 15
    int s = sum_int(arr, n);
    if (s != 15) return 1;

    // map_int: double each element
    int out[5];
    map_int(arr, n, out, double_it);
    if (out[0] != 2)  return 2;
    if (out[1] != 4)  return 3;
    if (out[2] != 6)  return 4;
    if (out[3] != 8)  return 5;
    if (out[4] != 10) return 6;

    // reduce_int: sum via add starting from 0
    int r = reduce_int(arr, n, 0, add);
    if (r != 15) return 7;

    // filter_int: keep even numbers (2, 4)
    int fout[5];
    size_t fcount = 0;
    filter_int(arr, n, fout, &fcount, is_even);
    if (fcount != 2)  return 8;
    if (fout[0] != 2) return 9;
    if (fout[1] != 4) return 10;

    return 42;
}
