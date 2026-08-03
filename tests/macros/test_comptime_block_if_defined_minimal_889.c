// Ticket #889 (minimal). The ticket's repro used #if defined(...) with an
// #else branch and pinned the bug on #else; that framing was incidental.
// This is the minimal trigger: a bare #if defined(...) with no #else and no
// #include, inside a comptime block, still desynced begin/end tracking
// before the fix (eval_const_expr's synthetic defined()-token substitution
// ran under a fresh File*, tripping the comptime auto-close check).
#pragma cccc comptime begin
#if defined(__APPLE__)
#endif
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
