// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: (?=[\s\S]*\bbreak;)(?=[\s\S]*\bcontinue;)
// CCCC_REJECT_STDOUT: goto \(null\)|goto \.L
//
// #1005: break/continue lower to an ND_GOTO node that sets only
// ->unique_label (a ".L..N" string, not a valid C identifier -- parse.c's
// new_unique_name()), leaving ->label NULL; a source-level `goto` sets
// ->label instead. serialize_stmt's ND_GOTO arm printed node->label
// unguarded, so break/continue reached fprintf's %s as NULL and emitted the
// literal text "goto (null);" -- a host compile error ("expected
// identifier"/"use of undeclared identifier 'null'") for any -c=native
// program with a break or continue inside a loop. Fixed by resolving
// ->unique_label against a jump-frame stack built while serializing
// loop/switch bodies and emitting the real C keyword instead.
int main(void) {
    int total = 0;
    for (int i = 0; i < 10; i++) {
        if (i == 5)
            break;
        if (i % 2 == 0)
            continue;
        total += i;
    }
    // i reaches 5 before the loop finishes; odd i in [0,5) are 1,3 -> 4
    return total == 4 ? 42 : 1;
}
