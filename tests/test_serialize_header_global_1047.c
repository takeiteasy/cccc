// Ticket #1047: under -c=native, a header-sourced global used to be
// re-emitted THREE times -- the replayed `#include` (which already defines
// it), the #918 forward-declare-every-global pass, and
// serialize_global_var()'s own definition -- a hard "redefinition" error
// from the host compiler. Functions already had an include-provenance gate
// (function_is_header_supplied(), src/serialize.c); globals had none.
//
// Fixed by adding global_is_header_supplied(), the global-side mirror of
// that function, and consulting it from both the forward-declare pass and
// serialize_global_var().
#include "test_serialize_header_global_1047.h"

int main(void) {
    if (header_global_1047 != 40)
        return 1;
    return header_global_1047 + 2;
}
