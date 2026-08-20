// [[cccc::shared]] and __attribute__((shared)) spellings work identically
// to @shared for #include.
#include[[cccc::shared]] < glob.h>

[[cccc::comptime]]
int glob_struct_nonempty(void) {
    return (int)sizeof(glob_t) > 0;
}

[[cccc::comptime]]
void generate_result(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(
        fn, MakeReturn(MakeIntLiteral(glob_struct_nonempty() ? 42 : 1)));
}

generate_result();

int main(void) {
    glob_t g;
    (void)g;
    return result();
}
