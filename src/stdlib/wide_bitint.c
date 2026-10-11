// Wide _BitInt runtime helpers for N in (64, 65535].
// All operations use little-endian word arrays: w[0] is the least-significant
// 64-bit word.  The caller allocates dst; all pointer args are uint64_t *.
// "words" is the number of 64-bit words (ceil(N/64), max 1025).
// After arithmetic ops, __cccc_bitint_trunc must be called on the result to
// enforce the exact bit width (mask the top word).
//
// The runtime itself is wide_bitint/runtime.c: compiled into the VM here and
// embedded as the shim text of `-c=native`/`-m`/`-c=generated` output, so both
// agree on wide-_BitInt arithmetic by construction. Only the VM registration
// below stays out of the shim.
#include "../cccc.h"
#include "wide_bitint/runtime.c"

// ---------- registration ----------

void register_wide_bitint_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "__cccc_bitint_add", (void *)__cccc_bitint_add, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_sub", (void *)__cccc_bitint_sub, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_mul", (void *)__cccc_bitint_mul, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_udiv", (void *)__cccc_bitint_udiv, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_umod", (void *)__cccc_bitint_umod, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_sdiv", (void *)__cccc_bitint_sdiv, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_smod", (void *)__cccc_bitint_smod, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_shl", (void *)__cccc_bitint_shl, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_ushr", (void *)__cccc_bitint_ushr, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_sshr", (void *)__cccc_bitint_sshr, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_cmp", (void *)__cccc_bitint_cmp, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_trunc", (void *)__cccc_bitint_trunc, 3,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_from_i64",
                      (void *)__cccc_bitint_from_i64, 4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_from_u64",
                      (void *)__cccc_bitint_from_u64, 4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_to_i64", (void *)__cccc_bitint_to_i64,
                      4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_to_double",
                      (void *)__cccc_bitint_to_double, 4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_from_double",
                      (void *)__cccc_bitint_from_double, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_extend", (void *)__cccc_bitint_extend,
                      7, 0);
    cc_register_cfunc(vm, "__cccc_bitint_and", (void *)__cccc_bitint_and, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_or", (void *)__cccc_bitint_or, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_xor", (void *)__cccc_bitint_xor, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_not", (void *)__cccc_bitint_not, 4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_neg", (void *)__cccc_bitint_neg, 4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_nonzero",
                      (void *)__cccc_bitint_nonzero, 2, 0);
    cc_register_cfunc(vm, "__cccc_bitint_from_str",
                      (void *)__cccc_bitint_from_str, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitfield_extract",
                      (void *)__cccc_bitfield_extract, 6, 0);
    cc_register_cfunc(vm, "__cccc_bitfield_insert",
                      (void *)__cccc_bitfield_insert, 4, 0);
}
