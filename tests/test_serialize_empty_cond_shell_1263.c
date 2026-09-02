// CCCC_FLAGS: -c=generated -o /dev/stdout -DFOO_1263=1 -DBAR_1263=1
// CCCC_EXPECT_STDOUT: int empty_shell_answer_1263\(void\)
// CCCC_REJECT_STDOUT: #(ifndef|ifdef|endif)
//
// Ticket #1263: a captured include-guard-style `#ifndef X` / `#define X` /
// `#endif` whose only body was a `#define` that a command-line `-D`
// pre-empted was replayed into `-c=generated` output as a vacuous
// `#ifndef` / `#endif` shell -- inert but noise. CCCC's own preprocessor
// had already resolved the conditional and skipped the redundant `#define`,
// so the shell carries no information; the serializer now drops any
// conditional span (nested ones too, via a depth counter) whose entire
// body was resolved away. The REJECT pattern fails if any `#if*`/`#endif`
// line survives. A live conditional -- see
// test_serialize_nonempty_cond_shell_1263.c -- is still emitted verbatim.

#ifndef FOO_1263
#define FOO_1263 1
#ifndef BAR_1263
#define BAR_1263 1
#endif
#endif

#include <stdio.h>

#pragma cccc comptime begin
[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("empty_shell_answer_1263", GetType("int"));
    FunctionSetBody(fn, Quote("return 42;"));
    PublishNode(fn);
}
gen();
#pragma cccc comptime end

int main(void) {
    return empty_shell_answer_1263();
}
