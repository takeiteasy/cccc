// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c89 -Wimplicit-function-declaration
// CCCC_EXPECT_STDERR: undefined function: missing
// CCCC_C4_SKIP: compile_only (-c) emits text relocations for unresolved symbols
// #1144: implicit function declaration is a hard error at C99+, which would
// mask the codegen-level "undefined function" failure this test actually
// targets (an implicit call that resolves to no FFI symbol at all) behind
// the parser's own error instead -- --std=c89 keeps the implicit
// declaration a warning so this test still reaches codegen.
int main(void) {
    return missing();
}
