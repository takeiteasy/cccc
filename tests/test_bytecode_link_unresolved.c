// EXPECT_COMPILE_ERROR
// CCCC_C4_SKIP
//
// Negative test for the bytecode linker pass (#565): an executable compiled
// with --link but with a CALL to a symbol not in any linked library must
// produce an "unresolved external" error, not silently emit a broken binary.
//
// This test file itself is a C source that we want cccc to error on when
// it tries to compile with an unresolved external symbol.
// CCCC_C4_SKIP: in -c mode (used by C4 round-trip), compile_only allows text
// relocations for undefined symbols (library semantics), so the error is
// intentionally deferred. This test covers exe-mode-only behaviour.

// Call a function that is declared but has no definition here, and we do NOT
// pass --link with a library that defines it.
// Because compile_only is false (we are not using -c bytecode), cccc must
// error immediately on the undefined function reference.

void undefined_external_fn(void);  // declaration only

int main(void) {
    undefined_external_fn();
    return 42;
}
