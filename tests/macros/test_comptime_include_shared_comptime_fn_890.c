// Ticket #890 (superseded by #894's demand-driven declaration index): an
// @shared-included header that also defines a [[cccc::comptime]] function.
// Under the old eager snapshot this header's declarations were forwarded
// twice -- once via the @shared replay, once via #890's own-file
// allowed-list -- and stayed benign only because repeated typedefs/
// tentative defs are legal C. The demand-driven index removes that
// redundancy entirely: @shared's textual replay is still the only thing
// that puts shared_plan_t/shared_plan_value in scope (nothing here is
// eagerly forwarded any more), so this now just confirms @shared routing
// itself still works end to end alongside a comptime-defining header.
#include @shared "comptime_shared_comptime_fn_890.h"

[[cccc::comptime]]
void generate_result_shared_890(void) {
    set_shared_plan();
    Obj *fn = MakeFunction("result_shared_890", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(shared_plan_value)));
}

generate_result_shared_890();

int main(void) {
    // shared_plan_t is also visible in the runtime TU via @shared.
    shared_plan_t p;
    p.x = 0;
    (void)p;
    return result_shared_890();
}
