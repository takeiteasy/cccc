// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: (?=[\s\S]*__builtin_strlen\()(?=[\s\S]*__builtin_strcmp\()(?=[\s\S]*__builtin_memset\()(?=[\s\S]*__builtin_memcpy\()(?=[\s\S]*__builtin_memmove\()(?=[\s\S]*__builtin_memcmp\()
// CCCC_REJECT_STDOUT: [^_]strlen\(|[^_]strcmp\(|[^_]memset\(|[^_]memcpy\(|[^_]memmove\(|[^_]memcmp\(
//
// #1154: __builtin_strlen/__builtin_strcmp/__builtin_mem{set,cpy,move,cmp}
// used to serialize as a plain call to the bare libc name (strlen(...),
// memcpy(...), ...) with no declaration ever emitted, so a native compile
// needed the caller's own #include <string.h> even though the VM path and
// real GCC/clang both need none. serialize_expr.c's ND_VAR case now spells
// these six the same way it already spelled __builtin_alloca: with their
// literal __builtin_ prefix, which every host compiler recognises as a
// builtin needing no header. This asserts the -m (dump-expanded) output
// shape directly: every one of the six calls below must be spelled with
// its __builtin_ prefix, and the bare libc name must never appear as a
// call. No #include at all -- see the REJECT_STDOUT/EXPECT_STDOUT above,
// which would catch a regression back to the bare spelling immediately.

int main(void) {
    char src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    char dst[8] = {0};

    __builtin_memcpy(dst, src, 8);
    if (__builtin_memcmp(src, dst, 8) != 0)
        return 1;

    __builtin_memmove(dst + 1, dst, 4);
    __builtin_memset(dst, 0, 8);

    const char *s1 = "hello";
    const char *s2 = "hello";
    if (__builtin_strlen(s1) != 5)
        return 2;
    if (__builtin_strcmp(s1, s2) != 0)
        return 3;

    return 42;
}
