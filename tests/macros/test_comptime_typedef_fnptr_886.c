// Ticket #886: a typedef declared inside comptime-executed code (whether
// inside a #pragma cccc comptime begin/end region, or annotated directly
// with [[cccc::comptime]]) is a type declaration, not a comptime variable --
// it must not be rejected by the ticket #188 pointer/string-variable check.

#pragma cccc comptime begin
#include <dlfcn.h>

// A function-pointer typedef: the original repro. Its declarator '*' must
// not be mistaken for a rejected pointer *variable*.
typedef int (*fn_t)(int);

// A plain typedef, to confirm the fix isn't narrowly scoped to function
// pointers: it must reach the macro program (via build_macro_context_tokens'
// declaration snapshot) so it's usable from a comptime function body.
typedef int myint;

static int add_one(int x) { return x + 1; }

void gen(void) {
    fn_t f = add_one;
    myint r = f(41);
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(r)));
}
gen();
#pragma cccc comptime end

// Also confirm the annotated spelling: this must not be misdetected as a
// bodyless comptime function declaration (which previously produced
// "comptime function 'int' declared but never defined").
[[cccc::comptime]] typedef int (*fn_t2)(int);

int main(void) {
    return result() == 42 ? 42 : 1;
}
