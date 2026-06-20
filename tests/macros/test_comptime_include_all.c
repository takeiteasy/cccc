// CCCC_FLAGS: --comptime-include-all
// With --comptime-include-all, plain #include declarations are forwarded to
// comptime (legacy all-headers behavior). glob_t must be usable in comptime.
#include <glob.h>

[[cccc::comptime]]
int glob_struct_nonempty(void) {
    return (int)sizeof(glob_t) > 0;
}

[[cccc::comptime]]
void generate_result(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(glob_struct_nonempty() ? 42 : 1)));
}

generate_result();

int main(void) {
    return result();
}
