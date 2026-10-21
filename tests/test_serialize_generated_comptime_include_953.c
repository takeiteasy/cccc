// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: struct LObj \{
// CCCC_REJECT_STDOUT: #include @comptime
// CCCC_REJECT_STDOUT: #include "fixtures/lobj_types_953b\.h"
// CCCC_HOST_CC
// -c=generated output for a struct reached only through #include @comptime
// still emits its definition and does not leak the include.
#include @comptime "fixtures/lobj_types_953b.h"
[[cccc::comptime]]
void gen(void) {
    Type *t  = GetType("LObj");
    Obj  *fn = MakeFunction("touch_lobj", GetType("int"));
    FunctionAddParam(fn, "p", MakePointer(t));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(0)));
    PublishNode(fn);
}
gen();
int main(void) {
    return touch_lobj((void *)0) == 0 ? 42 : 1;
}
