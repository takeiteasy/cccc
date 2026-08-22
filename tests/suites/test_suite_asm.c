// CCCC_FLAGS: --testing
// Consolidated suite: inline assembly
// Source tests: test_asm (moved here from test_suite_misc.c)
//
// This suite deliberately never round-trips through -c=native (NATIVE_SKIP_
// TESTS entry in tools/testing/__init__.py): the VM treats an ND_ASM
// statement as a no-op by default (--asm-passthru/--asm_callback opt into VM
// execution), while -c=native/-m serialize the string verbatim into plain
// `asm("...")` for the host assembler (src/serialize.c). There is no way to
// evaluate host assembly inside the VM -- it would mean either compiling the
// snippet separately and calling out to it, or parsing every host dialect
// into one uniform behaviour -- so the divergence cannot be merged from the
// VM side and is documented rather than papered over (COVERAGE.md,
// "Serialized-output divergences"). The fake-mnemonic cases below are the
// concrete blocker: no real assembler accepts them anywhere, so they can
// only ever run through the VM.
//
// Genuinely target-specific mnemonics are guarded with the arch macros
// CCCC's own preprocessor predefines from its host (__x86_64__/__i386__/
// __aarch64__/__arm64__, preprocess.c), so those strings only exist on
// hosts whose assembler actually accepts them; the guards are resolved at
// parse time, so nothing reaches the serializer for a non-matching host.
// That does not make this file natively compilable -- the fakes above see to
// that -- but it keeps the real-asm coverage honest about its target.

[[cccc::test(return = 42)]]
int test_asm(void) {
    int result = 10;
#if defined(__x86_64__) || defined(__i386__)
    asm("mov $42, %eax"); // AT&T x86 only; result register clobber is harmless
#endif
    asm("nop");
    asm volatile("nop");
    asm("instruction1"); // fake mnemonics: rejected by every real assembler;
    asm("instruction2"); // the VM must silently ignore both.
    result = 42;
    return result;
}
