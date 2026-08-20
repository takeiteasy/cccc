// CCCC_FLAGS: tests/fixtures/multi_tu_typedef_1006_a.c -m
// CCCC_C4_SKIP: multi-source compile, not a single-TU bytecode round-trip
// CCCC_EXPECT_STDOUT: (?=[\s\S]*#include <stdlib\.h>)(?=[\s\S]*typedef enum \{)
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// #1006: a typedef/struct/enum written *in* a non-primary translation unit
// (this file, input_files[1] -- fixtures/multi_tu_typedef_1006_a.c is
// input_files[0]/the primary file) used to be misclassified as
// header-supplied (record_type_name compared its declaring file against
// vm->compiler.primary_file, which only ever names input_files[0]) and its
// definition silently dropped from -c=native/-m output -- "unknown type
// name 'Thing'" from the host compiler even though nothing in this TU's own
// #includes supplies it either, since a non-primary TU's own #include
// directives were (independently) never auto-captured/replayed in the
// first place. This asserts both halves of the fix landed: the enum's
// definition is present, and its own #include is replayed ahead of it (the
// #include is otherwise unused here -- <stdlib.h> just stands in for "some
// header this non-primary TU needs"). tools/comptime_native_smoke.py's
// case is the load-bearing proof the resulting native binary actually
// links and runs.
#include <stdlib.h>
typedef enum { MULTI_TU_1006_ONE, MULTI_TU_1006_TWO } MultiTu1006Thing;

int multi_tu_typedef_1006_a(void);

static MultiTu1006Thing multi_tu_typedef_1006_pick(void) {
    return MULTI_TU_1006_TWO;
}

int main(void) {
    MultiTu1006Thing t = multi_tu_typedef_1006_pick();
    void            *p = malloc(8);
    free(p);
    return (t == MULTI_TU_1006_TWO ? 41 : 0) + multi_tu_typedef_1006_a();
}
