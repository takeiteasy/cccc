#include @shared <glob.h>

[[cccc::comptime]]
int glob_type_size(void) {
    glob_t g;
    return sizeof(g) > 0;
}

[[cccc::comptime]]
void generate_glob_context_result(void) {
    Obj *fn = MakeFunction("glob_context_result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(glob_type_size() ? 42 : 1)));
}

generate_glob_context_result();

int main(void) {
    return glob_context_result();
}

