// Ticket #684: #include @comptime "relative.h" must resolve a quoted,
// relative-to-this-file header path. Before the fix, the comptime-routed
// include was replayed under a synthetic filename with no directory context,
// so relative resolution always failed with "cannot open file".

#include @comptime "comptime_relative_include.h"

[[cccc::comptime]]
void generate_result(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn,
                    MakeReturn(MakeIntLiteral(comptime_relative_triple(14))));
}

generate_result();

int main(void) {
    return result();
}
