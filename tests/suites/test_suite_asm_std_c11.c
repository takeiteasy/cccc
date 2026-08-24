// CCCC_FLAGS: --testing --std=c11
// Coverage for #1130: asm is a GNU alternate keyword GCC disables under a
// strict ISO -std=cNN, so the serializer's old bare `asm(...)` emission
// became a syntax error on real GCC under `cccc --std=c11 -c=native` (Apple
// clang is lenient and does not reproduce the failure -- verify on Linux).
// The fix emits the __-wrapped __asm__ spelling everywhere, and the parser
// now accepts asm/__asm__/__asm as statement-position spellings too.
//
// Unlike tests/suites/test_suite_asm.c (a permanent NATIVE_SKIP_TESTS entry,
// #1119: fake mnemonics that only the VM can "run"), every string here is
// the empty asm(""), which every host assembler accepts -- this file IS
// natively compilable, and must round-trip clean under `--native` on both
// backends and under a real strict -std=c11 GCC (not just Apple clang).

int asm_label_fn(void) asm("asm_label_fn_impl");
int asm_label_fn(void) {
    return 42;
}

[[cccc::test(return = 42)]]
int test_asm_statement_spellings(void) {
    asm("");
    __asm__("");
    __asm("");
    asm volatile("");
    __asm__ volatile("");
    return 42;
}

[[cccc::test(return = 42)]]
int test_asm_symbol_label(void) {
    return asm_label_fn();
}
