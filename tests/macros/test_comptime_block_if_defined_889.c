// Ticket #889: a #if/#else/#endif written directly inside a
// #pragma cccc comptime begin/end block used to desync the block's
// begin/end tracking, reporting the block's own closing "end" as a
// "stray end without matching begin" error. The actual trigger was
// defined(...) inside the #if expression (see the minimal repro in
// test_comptime_block_if_defined_minimal_889.c) -- eval_const_expr's
// recursive preprocess2() call minted synthetic tokens under a fresh
// File*, which the comptime auto-close check mistook for "the file that
// opened the block has ended". This is the ticket's literal repro.
#pragma cccc comptime begin
#if defined(__APPLE__)
#define AOT_X 1
#else
#define AOT_X 2
#endif
#include <stdio.h>

void hello(void) {
    printf("hi\n");
}

int answer = 42;
#pragma cccc comptime end

[[cccc::comptime]]
Node *get_answer(void) {
    return GetComptimeVar("answer");
}

int main(void) {
    if (get_answer() != 42)
        return 1;
    return 42;
}
