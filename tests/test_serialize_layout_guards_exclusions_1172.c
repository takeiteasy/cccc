// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: disagreement: struct lg1172_holds_anon
// CCCC_EXPECT_STDOUT: disagreement: struct lg1172_plain_amid_includes
// CCCC_EXPECT_STDOUT: sizeof\(struct lg1172_plain_amid_includes\) == 16
// CCCC_REJECT_STDOUT: disagreement: struct lg1172_with_jmp
// CCCC_REJECT_STDOUT: disagreement: struct lg1172_with_ts
// CCCC_HOST_CC
// -m output skips layout guards for types that include setjmp or time structs,
// keeps them for anonymous holders and plain structs among includes, and the
// host compiler accepts it.

#include <setjmp.h>
#include <time.h>

// jmp_buf (include/setjmp.h: `typedef long long jmp_buf[40];`) is
// deliberately widened to a safe upper bound covering every supported host
// (#1054/#1059) -- CCCC's own folded size is intentionally NOT the real
// host's, so a guard here would fire on every host, gcc and clang alike. No
// guard must be emitted for a struct containing one.
struct lg1172_with_jmp {
    int     x;
    jmp_buf env;
};
struct lg1172_with_jmp g_jmp;

// struct timespec is from_include and defers to the host's own real layout
// (type_layout_is_host_owned()) -- also excluded.
struct lg1172_with_ts {
    int             x;
    struct timespec t;
};
struct lg1172_with_ts g_ts;

// A truly tagless, alias-less aggregate has no name to write the assert
// with at all -- documented residual, docs/NATIVE.md. Used only via a
// pointer here so it still needs a real (inline, unnamed) definition.
struct lg1172_holds_anon {
    struct {
        int x;
        int y;
    } *p;
};
struct lg1172_holds_anon g_anon;

// Regression guard for the bug this program's own exclusions caught while
// writing this case: an entirely ordinary struct with plain scalar members
// (no from_include/compiler-owned type anywhere in it) lost its guard
// ENTIRELY the moment the same TU also #include <time.h> -- a bare `int`/
// `char`/`long` member was handed to type_def_is_from_include_suppressed()
// without first requiring find_typedef_name_exact()'s pointer-identity
// match, so it spuriously matched some unrelated from_include typedef
// sharing the same representation (#1168's own bug, reopened in the sibling
// predicate type_contains_compiler_owned_layout()). This struct's guard
// must be present and correct despite <setjmp.h>/<time.h> both being
// #include'd above.
struct lg1172_plain_amid_includes {
    int  a;
    char b;
    long c;
};
struct lg1172_plain_amid_includes g_plain_amid_includes;

int main(void) {
    return 42;
}
