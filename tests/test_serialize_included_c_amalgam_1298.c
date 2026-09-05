// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: #include "fixtures/amalgam_1298_ops\.c"
// CCCC_REJECT_STDOUT: int amalgam_1298_add\(int
//
// #1298: an ordinary EXTERNAL-linkage function defined in a captured/
// replayed .c amalgamation (fixtures/amalgam_1298_ops.c, mirroring src/vm.c's
// own `#include "ops.c"`) used to be re-serialized on top of the replayed
// #include line, since function_is_header_supplied() (src/serialize_program.c)
// only ever suppressed a `static` definition (#999) -- an external-linkage
// one got independently re-emitted, a host "redefinition" error the moment
// the replayed #include and the re-serialized definition both reached the
// output. This test only checks -m's shape (the definition must not appear
// a second time in this TU's own output, since the replayed #include already
// supplies it); tools/comptime_native_smoke.py's multi-TU case is what proves
// the resulting -c=native output actually links.
#include "fixtures/amalgam_1298_ops.c"

int main(void) {
    return amalgam_1298_add(41);
}
