// #1298 (residual, found via #1132's round-13 self-hosting spike):
// function_is_header_supplied()'s external-linkage arm keyed its
// path_is_c_source_file()/path_is_captured() check on obj->tok alone --
// but cc_link_progs (#957) merges an external-linkage Obj across every TU
// that so much as declares it, and the merged Obj's own representative
// token (obj->tok) can end up pointing at whichever TU's declaration the
// linker kept last. This fixture's shape (src/internal.h's own): TU A
// (this file's own pair, fixtures/amalgam_1298_multi_tu_a.c) #includes both
// the shared prototype header AND the .c amalgamation defining the
// function; TU B (fixtures/amalgam_1298_multi_tu_b.c) only #includes the
// shared header and calls the function, never re-including the
// amalgamation itself -- reaching it purely through the bodiless
// prototype, same as src/codegen_addr.c reaches src/vm.c's ops.c-defined
// functions through src/internal.h. When the merged Obj's obj->tok ends up
// tracing to the shared header (not the amalgamation), the external-
// linkage arm never fires and the function is re-serialized on top of the
// replayed #include, a host "redefinition" error -- fixed by also
// consulting obj->body's own token (which always traces to the file that
// genuinely contains the definition) when obj->tok's own file doesn't
// already qualify.
// CCCC_FLAGS: tests/fixtures/amalgam_1298_multi_tu_b.c
#include "fixtures/amalgam_1298_shared.h"
#include "fixtures/amalgam_1298_ops.c"

int amalgam_1298_multi_tu_call_a(void) {
    return amalgam_1298_add(20);
}
