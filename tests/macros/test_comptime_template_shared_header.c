// A template in an @shared header is defined in both programs: comptime code
// instantiates its own copy, including specializations and partials.
#include @shared "comptime_templates.h"

@comptime int ct_value(void) {
    @inst(int *) Tag p = {1, 2};
    @inst(char) Tag  c = {3, 'x'};
    return @inst(int) twice(10) + sizeof(@inst(int, 3) Arr) / sizeof(int) +
           p.ptr + c.kind;
}

@comptime void gen(void) {
    Obj *fn = MakeFunction("answer", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(ct_value())));
}
gen();

int answer(void);

int main(void) {
    return answer() + @inst(int) twice(9) - 4; // 28 + 18 - 4
}
