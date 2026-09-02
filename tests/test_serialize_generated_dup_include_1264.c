// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: #include <stdio\.h>
// CCCC_REJECT_STDOUT: #include <stdio\.h>[\s\S]*#include <stdio\.h>
//
// Ticket #1264: top-level preprocessor directives were recorded into two
// parallel channels -- `push_emit_directive` -> `emit_directives` (the
// -c=native / -m replay, which dedups identical `#include` lines) and
// `cc_record_emit_source` -> `emit_events` (the -c=generated replay, which
// did not). A single primary file including the same header twice therefore
// emitted one `#include <stdio.h>` under -c=native but two under
// -c=generated. Fixed by deduping identical `#include` lines in the
// contiguous unconditional leading run of the generated replay
// (line_is_include_directive, src/serialize_program.c) -- where dropping one
// cannot leave an empty conditional shell. The REJECT pattern fails if the
// same include is emitted twice; EXPECT_STDOUT alone would not, being a
// presence match.
#include <stdio.h>
#include <stdio.h>

#pragma cccc comptime begin
[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("dup_include_answer_1264", GetType("int"));
    FunctionSetBody(fn, Quote("return 42;"));
    PublishNode(fn);
}
gen();
#pragma cccc comptime end

int main(void) {
    return dup_include_answer_1264();
}
