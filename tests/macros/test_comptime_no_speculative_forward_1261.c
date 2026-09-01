// #1261: `#include @shared` of a runtime-only module leaves its prototypes
// bodyless in the comptime program. The #1243 body-forwarder used to
// speculatively splice a definition for *every* bodyless prototype, not just
// the ones comptime code calls -- and rt_log's body cannot parse in the
// isolated comptime context (it calls into <stdio.h>, never routed to
// comptime), so the forward left a half-built error AST that segfaulted
// Step-2 codegen of the comptime program (a NULL deref in
// addr_is_local_frame).
//
// Forwarding is now demand-driven: gen() below calls only rt_add, so only
// rt_add's body is pulled in; rt_touch / rt_log are left alone. The
// program must compile and f() must fold to 42 at compile time.
//
// CCCC_FLAGS: tests/macros/comptime_shared_rt_1261.c
#include @shared "comptime_shared_rt_1261.h"

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("f", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(rt_add(20, 22))));
}
gen();

int main(void) {
    // rt_touch / rt_log are genuine runtime code here -- they link fine,
    // they just never had any business being in the comptime program.
    if (rt_touch() != 50)
        return 1;
    return f();
}
