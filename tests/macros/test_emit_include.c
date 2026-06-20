// Test #include [[cccc::emit]]: the directive is routed to serialized output
// without entering the runtime translation unit.

#include [[cccc::emit]] <stddef.h>
#include [[cccc::emit]] <stddef.h>

[[cccc::comptime]]
void gen_answer(void) {
    Obj *fn = MakeFunction("get_answer", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
}

gen_answer();

int get_answer(void);

int main(void) {
    return get_answer();
}
