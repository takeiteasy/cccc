// Test #pragma cccc emit captures raw preprocessor directives for serialized
// output.

#pragma cccc comptime begin
#pragma cccc emit begin
#ifdef __CCCC_TEST_EMIT_BLOCK__
#define EMIT_BLOCK_FN(x) x
#endif
#pragma cccc emit end
#pragma cccc comptime end

[[cccc::comptime]]
void gen_answer(void) {
    Obj *fn = MakeFunction("emit_block_answer", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
}

gen_answer();

int emit_block_answer(void);

int main(void) {
    return emit_block_answer();
}
