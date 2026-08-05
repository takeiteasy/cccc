// Ticket #894: a plain (unrouted) #include of a header that defines no
// comptime code of its own -- the residual gap #890's file-identity filter
// left behind. Point's typedef reaches the comptime body here purely on
// demand: nothing forwards it up front, and the include needs no @shared.
#include "comptime_decl_index_third_894.h"

[[cccc::comptime]]
int check(void) {
    Point p;
    p.x = 40;
    p.y = 2;
    return p.x + p.y;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(check())));
}
gen();

int main(void) {
    return result();
}
