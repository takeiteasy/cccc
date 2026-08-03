// Ticket #889 (__has_include variant). __has_include(...) inside a #if
// expression goes through the same synthetic-token substitution in
// read_const_expr/eval_const_expr as defined(...) does, so it hit the same
// comptime-block auto-close bug.
#pragma cccc comptime begin
#if __has_include(<stdio.h>)
int answer = 42;
#else
int answer = 1;
#endif
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
