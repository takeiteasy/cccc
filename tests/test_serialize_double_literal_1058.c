// Ticket #1058: serialize_expr's ND_NUM TY_DOUBLE arm printed a bare
// `%.17g` of the folded value with no ".0"-if-integral fixup -- unlike the
// TY_FLOAT/TY_LDOUBLE arms right next to it, which already apply exactly
// that fixup (#1038, format_float_literal/format_ldouble_literal). An
// integral double like 55.0 therefore serialized as the text "55", which a
// real host compiler reads back as an *integer* literal, not a double one.
//
// Harmless wherever an enclosing (double) cast/coercion forces the
// conversion anyway (an ordinary assignment or arithmetic context), but a
// real wrong answer wherever the literal's own text is what supplies its
// type to a variadic call site -- the real host compiler decides which
// registers/slots to place each variadic argument in based on the
// *argument expression's own type*, so an unsuffixed "55" is passed as an
// int, not a double, regardless of which parameter it's ultimately headed
// for. Deliberately uses a real host variadic function (snprintf, reached
// via the auto-captured `#include <stdio.h>`) rather than a
// CCCC-implemented variadic function -- this exercises the call-site
// argument-marshalling path independently of #1018's stdarg.h/va_list
// translation (a separate, not-yet-fixed gap), isolating this ticket's own
// defect. Found while root-causing #1018 (tests/repro_varargs.c).
//
// Confirmed against the pre-fix binary: -c=native on this exact file
// mis-prints "55.0"/"100.0"/etc as bare "55"/"100", snprintf reads a
// garbage double from the wrong slot, and this test returns non-42.

#include "stdio.h"
#include "string.h"

int main() {
    char buf[64];

    // Every literal here is integral-valued in double form -- exactly the
    // shape that lost its ".0" pre-fix.
    snprintf(buf, sizeof buf, "%g", 55.0);
    if (strcmp(buf, "55") != 0)
        return 1;

    snprintf(buf, sizeof buf, "%g %g %g", 0.0, 100.0, -58.0);
    if (strcmp(buf, "0 100 -58") != 0)
        return 2;

    return 42;
}
