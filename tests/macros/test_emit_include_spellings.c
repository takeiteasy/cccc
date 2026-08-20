// Test alternate include qualifier spellings for emit routing.

#include @emit < stddef.h>
#include __attribute__((emit)) < stdint.h>

[[cccc::comptime]]
void gen_answer(void) {
    Obj *fn = MakeFunction("emit_spelling_answer", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
}

gen_answer();

int emit_spelling_answer(void);

int main(void) {
    return emit_spelling_answer();
}
