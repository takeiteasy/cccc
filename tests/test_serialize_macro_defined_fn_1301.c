// #1301 (negative guard): a captured header's macro that *produces* a
// function definition when invoked, rather than writing the function
// itself -- fixtures/macro_defined_fn_1301_shared.h's V1301_DEFINE_THING(T)
// expands to `int thing_##T(void) { return 21; }`. Invoked here (a
// command-line-input file) as V1301_DEFINE_THING(int), the resulting
// declarator name's *spelling* location is the header (where the macro is
// defined), but its *expansion* location -- what function_is_header_
// supplied() must actually key its decision on -- is this file. Getting
// this wrong reintroduces the exact pthread_once-class failure #1298 hit:
// thing_int()'s body would be silently dropped as "already header-
// supplied", an "undeclared identifier" at fixtures/
// macro_defined_fn_1301_b.c's own call site. This is the test that
// documents why function_is_header_supplied() walks Token.origin at all --
// without it, a future refactor could drop the walk and this would still
// pass under -m alone (this file's own TU never notices its own body is
// missing) but fail to link under -c=native, which is why the
// CCCC_FLAGS-listed second TU below (that calls thing_int() through a
// plain declaration) is required to expose it.
// CCCC_FLAGS: tests/fixtures/macro_defined_fn_1301_b.c
#include "fixtures/macro_defined_fn_1301_shared.h"

V1301_DEFINE_THING(int)

int thing_caller_1301(void) {
    return thing_int();
}
