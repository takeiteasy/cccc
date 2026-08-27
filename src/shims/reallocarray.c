// -c=native reallocarray shim (#1155): reached via serialize_expr.c's
// ND_FUNCALL remap, not a same-name macro, so it lives outside the
// native_accessor table.
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating and rationale live in the
// matching serialize_*_shims() in src/serialize_shims.c.

// >>> shim: body
#include <errno.h>
static void *__cccc_reallocarray(void *__cccc_p, size_t __cccc_n, size_t __cccc_s) {
    if (__cccc_n && __cccc_s > (size_t)-1 / __cccc_n) {
        errno = ENOMEM;
        return 0;
    }
    return realloc(__cccc_p, __cccc_n * __cccc_s);
}
// <<< shim
