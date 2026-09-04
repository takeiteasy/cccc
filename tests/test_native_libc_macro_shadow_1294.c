// -c=native regression (#1294): a bodiless declaration reached through an
// uncaptured bundled header (see native_libc_macro_shadow_1294.h's own
// comment) used to collide with a host libc name spelled as a function-like
// macro. Two families:
//
//   - getc_unlocked/getchar_unlocked/putc_unlocked/putchar_unlocked: the
//     macOS SDK's <_stdio.h> spells these as function-like macros
//     (`#define getchar_unlocked() getc_unlocked(stdin)` etc), so CCCC's
//     own plain-prototype fallback expanded through the macro instead of
//     declaring the function ("unknown type name '__stdoutp'", "too many
//     arguments provided to function-like macro invocation", "redefinition
//     of 'getchar_unlocked' as different kind of symbol").
//   - snprintf/sprintf/vsnprintf/vsprintf: macOS's <secure/_stdio.h>
//     rewrites these to __builtin___*_chk under the same mechanism
//     ("expected parameter declarator", "conflicting types for
//     '__builtin___vsnprintf_chk'").
//
// Fixed by parenthesizing a bodiless declaration's declarator
// (serialize_function_signature, src/serialize_decl.c): `(name)(params)`
// defeats a function-like macro of the same name without giving up the
// prototype. getc_unlocked/getchar_unlocked would block on stdin if
// actually called, so they're only referenced (never invoked) behind an
// always-false volatile guard -- is_used still fires at parse time, so
// their prototypes are still emitted and still exercise the bug.
#include "native_libc_macro_shadow_1294.h"

static volatile int g_never = 0;

static int vfmt(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

static int vfmt_unbounded(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int main(void) {
    char buf[32];
    int  n = snprintf(buf, sizeof buf, "%d", 40);
    sprintf(buf, "%d", 40);
    vfmt(buf, sizeof buf, "%d", 40);
    vfmt_unbounded(buf, "%d", 40);

    putchar_unlocked('\0'); // writes a NUL, not visible test output
    putc_unlocked('\0', stdout);

    if (g_never) {
        getc_unlocked(stdin);
        getchar_unlocked();
    }

    return n == 2 ? 42 : 1;
}
