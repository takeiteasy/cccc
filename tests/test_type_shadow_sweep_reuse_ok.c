// CCCC_FLAGS: --type-checks --vm-profile
// CCCC_REJECT_STDERR: TYPE MISMATCH DETECTED
// Ticket #767: freeing a swept (all-zero) page must be observationally
// identical to leaving it allocated -- every shadow reader already treats a
// NULL page as all TY_VOID. Reallocating over the same range right after
// the sweep and stamping it as a different type must not false-positive
// against a page type_shadow_sweep freed out from under it.
#include <stdlib.h>

int main(void) {
    size_t n = 48 * 1024; // matches test_type_shadow_sweep_ok.c's page-spanning size
    int *arr = malloc(n * sizeof(int));
    for (size_t i = 0; i < n; i++)
        arr[i] = (int)i; // stamps every byte in range as int
    free(arr);            // clears + sweeps the tail page

    // Same-size reuse: likely lands on (some of) the just-freed range,
    // including whatever page the sweep reclaimed.
    double *d = malloc(n * sizeof(int));
    double sum = 0.0;
    for (size_t i = 0; i < n / 2; i++) {
        d[i] = (double)i * 1.5; // stamps as double
        sum += d[i];
    }
    free(d);
    return (sum > 0.0) ? 42 : 1;
}
