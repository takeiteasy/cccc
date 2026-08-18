// Ticket #1018 follow-up: va_arg(ap, T) printed T verbatim from the
// user's own macro argument text, but a real host compiler's own
// <stdarg.h> diagnoses an unpromoted promotable T as undefined behavior --
// "second argument to 'va_arg' is of promotable type 'float'/'char'/...;
// this va_arg has undefined behavior because arguments will be promoted
// to 'double'/'int'" -- since C's default argument promotions (C17
// 6.5.2.2p6/7) mean a real variadic call site never actually places a
// bare float/char/short/bool in a variadic slot; the caller already
// promoted it to double/int before the call. CCCC's own VM-ABI read
// always reads a full 8-byte slot regardless of the requested width, so
// this had no VM-visible symptom (this file passes on the VM path both
// before and after the fix) -- only a native compiler diagnostic, found
// by a second reviewer during a post-#1018 self-check.
//
// Fixed by va_arg_promoted_type() (src/serialize.c): the VA_ARG print
// site now maps TY_FLOAT -> double and any integer type narrower than
// int -> int before printing, mirroring type.c's own integer_promotion()
// for the integer half. Confirmed against real clang -c=native: -Wvarargs
// fires on the pre-fix binary's output for both the float and char arms
// below, and is silent post-fix.

#include "stdarg.h"

static float sum_floats(int n, ...) {
    va_list ap;
    va_start(ap, n);
    float total = 0.0f;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, float);
    va_end(ap);
    return total;
}

static int sum_chars(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, char);
    va_end(ap);
    return total;
}

static int sum_shorts(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, short);
    va_end(ap);
    return total;
}

int main() {
    // C default-argument-promotion rules already promote each of these
    // literal args to double/int at the call site regardless of the
    // parameter type text used inside va_arg -- exactly the shape that
    // was unsafe to print verbatim under -c=native.
    float f = sum_floats(2, 1.5f, 2.5f);
    if (f != 4.0f)
        return 1;

    int c = sum_chars(3, (char)10, (char)20, (char)12);
    if (c != 42)
        return 2;

    int s = sum_shorts(2, (short)100, (short)-58);
    if (s != 42)
        return 3;

    return 42;
}
