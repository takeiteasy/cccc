// #1144: __builtin_memset/memcpy/memmove/memcmp -- GCC/clang recognise
// these as ordinary aliases of the libc functions of the same name (minus
// the __builtin_ prefix), needing no #include <string.h> at all (verified
// against real clang directly). Forwarded via a private stub Obj, the same
// pattern __builtin_strlen/__builtin_strcmp already use (parse_decl.c) --
// each stub's `const void *` parameter must copy ty_void before marking it
// const, not mutate the shared singleton in place (a first attempt at this
// did exactly that and corrupted every plain `void` in the rest of the
// compile, caught by this suite's own comptime_native_smoke ctor/dtor case
// turning `void plain_ctor` into `const void plain_ctor`).
//
// No #include <string.h> anywhere in this file, deliberately: the VM path
// never needed one (test_serialize_uchar_shims_1141.c exercises that,
// VM-only via -m), and since #1154, -c=native/-m/-c=generated don't either
// -- serialize_expr.c's ND_VAR case now prints these calls (and
// __builtin_strlen/__builtin_strcmp) using their literal __builtin_
// spelling, the same way it already did for __builtin_alloca, so the host
// compiler recognises them as builtins needing no prototype.

int main(void) {
    char src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    char dst[8];

    __builtin_memcpy(dst, src, 8);
    if (dst[0] != 1 || dst[7] != 8)
        return 1;
    if (__builtin_memcmp(src, dst, 8) != 0)
        return 2; // copy must be byte-identical

    __builtin_memmove(src + 1, src, 4);
    if (src[1] != 1 || src[4] != 4)
        return 3; // overlapping copy, src now {1,1,2,3,4,6,7,8}

    if (__builtin_memcmp(src, dst, 8) == 0)
        return 4; // src and dst must have diverged after the memmove

    __builtin_memset(dst, 0, 8);
    if (dst[0] != 0 || dst[7] != 0)
        return 5;

    return 42;
}
