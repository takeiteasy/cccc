// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int \(abs\)\(int
//
// #1303 (bodyless_decl_from_input_or_bundled() counterpart to #1301's
// function fix): a captured header's macro that only *declares* (never
// defines) a function when invoked --
// fixtures/macro_declared_fn_1303_shared.h's V1303_MK_NAME(x) expands to
// `ab##x`, pasting the header's own "ab" literal onto this file's own
// argument "s" to name the real host libc function `abs`. The resulting
// declarator name's *spelling* location is the header (where the paste's
// left-hand literal is written), but its *expansion* location -- what
// bodyless_decl_from_input_or_bundled() must actually key its decision on
// -- is this file. Getting this wrong drops the declaration entirely (read
// as "already supplied by this header's own replayed #include", which
// never contains any declaration of abs), a host "call to undeclared
// library function 'abs'" error under -c=native. Confirmed via a
// standalone repro before the fix walked Token.origin's expansion site the
// same way function_is_header_supplied() already did.
#include "fixtures/macro_declared_fn_1303_shared.h"

int V1303_MK_NAME(s)(int);

int main(void) {
    return V1303_MK_NAME(s)(-42) == 42 ? 42 : 1;
}
