// Float-to-integer conversions agree between the VM and -c=native.
#include <fenv.h>
int main(void) {
    volatile double d_in_range = 1.5e19;
    volatile double d_neg_inf  = -1.0 / 0.0;
    volatile float  f_in_range = 1.5e19f;

    feclearexcept(FE_ALL_EXCEPT);
    unsigned long long u = (unsigned long long)d_in_range;
    if (u != 15000000000000000000ULL)
        return 1;
    if (fetestexcept(FE_INVALID))
        return 2; // must NOT raise: in-range

    long long r = (long long)d_neg_inf;
    if (r != (-9223372036854775807LL - 1))
        return 3; // saturates

    unsigned long long uf = (unsigned long long)f_in_range;
    if (uf != 15000000520515485696ULL)
        return 4;

    return 42;
}
