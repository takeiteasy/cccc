// EXPECT_COMPILE_ERROR
// Regression test for ticket #144: equal()/skip() NULL dereference
// on truncated/malformed input. Before the fix, truncated files
// ending mid-expression crashed with a SEGV in tokenize.c:287
// when equal() received a NULL Token pointer.

int main() {
    if (1)
        ret
}
