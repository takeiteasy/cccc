// Checked regions (#485): a region opened around a #include must NOT apply
// to the header's own declarations -- every libc prototype/struct member is
// an unchecked pointer and cccc ships no annotated headers, so a checked
// region spanning a header would otherwise reject the standard library
// outright. checked_scope_for_stamp() (src/preprocess.c) gates the stamp to
// cc_file_is_command_line_input() tokens only.
//
// <locale.h>'s `struct lconv` is the sharpest available probe: it has
// several bare `char *` members (decimal_point, thousands_sep, ...)
// declared inside the header. If the header-contamination gate were broken,
// merely #include'ing this header while a checked region is open would fail
// to compile -- struct_members()'s declaration ban (src/parse_types.c)
// would fire on decimal_point etc. the moment the struct definition's
// tokens were parsed, entirely independent of anything in this file's own
// code below.

#pragma cccc checked begin

#include <locale.h>
#include <stdio.h>

int main(void) {
    // localeconv()'s return type is an unchecked `struct lconv *` -- capture
    // it inside an unchecked block rather than a bare local here, since a
    // local of that type in *this* file (not a header) is correctly banned
    // by the region regardless of the header-contamination question this
    // test targets.
    [[cccc::unchecked]] {
        struct lconv *lc = localeconv();
        (void)lc;
    }
    printf("ok\n");
    return 42;
}

#pragma cccc checked end
