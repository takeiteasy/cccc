#include @shared "macro_context_local.h"

[[cccc::comptime]]
int local_header_type_size(void) {
    macro_context_local_t item;
    item.value = 42;
    return sizeof(item) > 0 && item.value == 42;
}

[[cccc::comptime]]
void generate_local_header_context_result(void) {
    Obj *fn = MakeFunction("local_header_context_result", GetType("int"));
    FunctionSetBody(
        fn, MakeReturn(MakeIntLiteral(local_header_type_size() ? 42 : 1)));
}

generate_local_header_context_result();

int main(void) {
    return local_header_context_result();
}
