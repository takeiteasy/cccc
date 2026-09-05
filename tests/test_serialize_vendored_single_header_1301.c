// #1301 (follow-up to #1298's own residual, shape 2 -- deliberately left
// out of that fix): function_is_header_supplied() (src/serialize_program.c)
// keyed its external-linkage arm on a plain `.c`-extension check, gated
// narrowly to the textual-amalgamation shape (src/vm.c's `#include
// "ops.c"`), and deliberately excluded a vendored single-header library
// (a `.h` built via an `#define FOO_IMPLEMENTATION`-style macro in exactly
// one TU, mirroring src/stdlib/format_printf.c's own vendored stb_sprintf.h)
// because its public functions' declarator names are macro-token-pasted
// (fixtures/vendored_1301_lib.h's `V1301_DECORATE(name)`) -- a
// file-identity check on the pasted token's own spelling location (the
// header, where the macro is *defined*) can't be trusted to also be its
// *expansion* location (where the macro is *invoked*) in general, though
// here they happen to coincide.
//
// This file (this TU) defines VENDORED_1301_IMPLEMENTATION and includes
// the header, so its own v1301_add() definition is captured/replayed along
// with the #include. fixtures/vendored_1301_b.c (this test's own
// CCCC_FLAGS-listed second TU) never re-includes the header at all --
// only a plain declaration -- and calls v1301_add() through it, mirroring
// src/codegen_addr.c's own relationship to src/vm.c's ops.c-defined
// functions via src/internal.h. Before the fix, v1301_add() was
// re-serialized independently in THIS TU's own output on top of the
// replayed #include, a host "redefinition of 'v1301_add'" error --
// confirmed pre-fix. tools/comptime_native_smoke.py's
// case_vendored_single_header_1301 is the load-bearing VM-42-to-native-42
// proof; this file's own -m shape assertion can't see a link failure.
// CCCC_FLAGS: tests/fixtures/vendored_1301_b.c
#define VENDORED_1301_IMPLEMENTATION
#include "fixtures/vendored_1301_lib.h"

int vendored_1301_call_a(void) {
    return v1301_add(20);
}
