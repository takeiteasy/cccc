// --use-system-headers --no-builtin-includes: complex.h is compiler-owned
// and always resolves to CCCC's own creal/cimag/CMPLX, never the SDK's.
// A real SDK complex.h compiles cleanly (unlike setjmp.h's incompatible
// layout) but its creal/cimag are ABI-incompatible with CCCC's VM, which
// lowers complex arithmetic through __cccc_creal/__cccc_cmplx builtins
// instead of passing a real _Complex value across a call boundary.
// CCCC_FLAGS: --use-system-headers --no-builtin-includes
#include <complex.h>

int main(void) {
    double complex z = CMPLX(3.0, 4.0);
    double r = creal(z);
    double i = cimag(z);
    return (int)(r + i) == 7 ? 42 : 1;
}
