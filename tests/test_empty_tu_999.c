// CCCC_FLAGS: tests/fixtures/empty_tu_999_defs.c
// CCCC_C4_SKIP: multi-source compile, not a single-TU bytecode round-trip
// CCCC_REJECT_STDERR: failed to parse
//
// #999: cc_parse() returns NULL when a TU creates zero new globals --
// legitimate for a TU (tests/fixtures/empty_tu_999_defs.c, listed first via
// CCCC_FLAGS) holding only a typedef. main.c's per-TU parse loop used to
// treat any NULL prog as an unconditional failure, printing "error: failed
// to parse <file>" and bailing with exit_code left at its default 0 --
// silent success dressed up as a bogus failure message. Verified this
// exact program printed that error and exited 0 (not 42) before the fix.
int main(void) {
    return 42;
}
