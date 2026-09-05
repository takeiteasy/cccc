// TU B for tests/test_serialize_included_c_amalgam_multi_tu_1298.c (#1298
// residual). Only #includes the shared prototype header -- never the .c
// amalgamation itself -- and calls the function purely through that
// bodiless declaration, mirroring src/codegen_addr.c's own relationship to
// src/vm.c's ops.c-defined functions via src/internal.h.
#include "amalgam_1298_shared.h"

int amalgam_1298_multi_tu_call_a(void);

int main(void) {
    return amalgam_1298_multi_tu_call_a() + amalgam_1298_add(20);
}
