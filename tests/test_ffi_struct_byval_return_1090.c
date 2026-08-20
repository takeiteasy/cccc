// Expected return: 42
// #1090 (found auditing #1087): a raw FFI registration of a host function
// whose C ABI *returns* a struct/union by value is also wrong, the mirror
// of the by-value-argument bug in test_ffi_struct_byval_arg_1090.c. CCCC's
// own calling convention always returns a struct/union as a pointer in
// REG_A0 (the RETBUF convention), not the host ABI's in-register struct
// return. div()/ldiv()/lldiv() (src/stdlib/stdlib.c) were registered raw
// -- confirmed failing pre-fix: div() returned garbage, ldiv()/lldiv()
// SIGSEGV'd (exit 139) the moment their 16-byte div_t was read through
// whatever REG_A0 happened to hold. Fixed with wrap_div/wrap_ldiv/
// wrap_lldiv, which copy the host result into a small thread-local
// rotating pool (mirroring the VM's own RETBUF rotation) and return a
// pointer to the current slot.
#include <stdlib.h>

// Forces both div() calls to be evaluated as arguments of the same call
// expression, each result then copied into its own by-value argument slot
// -- a single static result buffer (instead of the rotating pool) would
// let the second div() call's write clobber the first's result before it
// is copied out, since both are outstanding at once.
static int check_two(div_t a, div_t b) {
    return (a.quot == 14 && a.rem == 2 && b.quot == 2 && b.rem == 1) ? 1 : 0;
}

int main(void) {
    div_t d = div(17, 5);
    if (d.quot != 3 || d.rem != 2)
        return 1;

    ldiv_t l = ldiv(-17L, 5L);
    if (l.quot != -3L || l.rem != -2L)
        return 2;

    lldiv_t ll = lldiv(17LL, -5LL);
    if (ll.quot != -3LL || ll.rem != 2LL)
        return 3;

    if (!check_two(div(100, 7), div(9, 4)))
        return 4;

    return 42;
}
