// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: [^_]getline\(\)
// CCCC_REJECT_STDOUT: getline__cccc_dup[0-9]+
//
// #1103: rename_colliding_static_names()'s host-libc-symbol probe (Tier A,
// #1042(c)) renames ANY static, defining Obj whose name dlsym finds in the
// host's own libc -- including a `static inline` function declared in a
// non-primary source file (a header), whose DEFINITION never actually
// reaches -c=native/-m output at all: function_is_header_supplied()
// suppresses it, on the assumption that the header's own (replayed)
// #include supplies it under the ORIGINAL name. Before this fix, every
// call site got renamed anyway (every reference resolves through the same
// Obj*), so the emitted C called an identifier nothing declared --
// "use of undeclared identifier 'name__cccc_dupN'". Reproduced for real by
// include/ndbm.h's five `static inline dbm_*` shims (dbm_store et al); this
// fixture uses a synthetic `getline` shim
// (tests/fixtures/header_static_1103.h) instead of ndbm.h so the test
// doesn't depend on libgdbm-compat/CCCC_HAS_NDBM being available on every
// CI host. Fixed by skipping the rename entirely once
// function_is_header_supplied() is true -- see
// rename_colliding_static_names()'s own #1103 comment (src/serialize.c).
#include <math.h> // any real (non-cccc-only) #include, just to arm the dlsym
                  // probe at all (any_real_include_replayed()) -- unrelated
                  // to the colliding name itself.
#include "fixtures/header_static_1103.h"

int main(void) {
    return getline();
}
