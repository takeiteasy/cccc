// #include @shared makes a header available in both runtime and comptime.
// glob_t must be usable in comptime AND in the runtime TU.
#include @shared <glob.h>

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
    // glob_t is also visible in the runtime TU because @shared splices into both.
    glob_t g;
    (void)g;
    return result();
}
