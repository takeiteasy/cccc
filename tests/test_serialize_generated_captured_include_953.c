// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: touch_lobj
// CCCC_EXPECT_STDOUT: #include "fixtures/lobj_types_953\.h"
// CCCC_REJECT_STDOUT: struct LObj \{
// CCCC_REJECT_STDOUT: typedef struct LObj
// CCCC_HOST_CC: -Itests
// -c=generated output for a GetType()d struct reached through a plain #include
// has no duplicate definition.
#include "fixtures/lobj_types_953.h"
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
