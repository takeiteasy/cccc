// Checked regions: NULL and the sanctioned bounds casts compile in a pragma
// region under --checked-pointers (propagation and cast desugaring run).
// CCCC_FLAGS: --checked-pointers

#include <stddef.h>

int *g_raw;

#pragma cccc checked begin
int f(int n) {
    int *[[cccc::single]] z = NULL;
    int *[[cccc::array, cccc::count(n)]] a =
        (int *[[cccc::array, cccc::count(n), cccc::assume]])g_raw;
    int *[[cccc::array, cccc::count(n)]] b = a;
    return z == NULL ? b[0] : 0;
}
#pragma cccc checked end

int main(void) {
    int arr[3] = {42, 2, 3};
    g_raw      = arr;
    return f(3);
}
