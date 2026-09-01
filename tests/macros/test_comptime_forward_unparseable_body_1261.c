// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no body is available to the comptime pass
//
// #1261 companion to test_comptime_no_speculative_forward_1261.c: when
// comptime code *does* call a forwarded function whose body genuinely cannot
// be compiled in the isolated comptime context (rt_log calls into <stdio.h>,
// which is never routed to comptime), the failed splice must rewind cleanly
// -- leaving no partial body on the prototype Obj -- and the call must
// degrade to the ordinary "no body available" diagnostic, not a crash.
//
// CCCC_FLAGS: tests/macros/comptime_shared_rt_1261.c
#include @shared "comptime_shared_rt_1261.h"

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(rt_log("hi"))));
}
gen();

int main(void) {
    return f();
}
