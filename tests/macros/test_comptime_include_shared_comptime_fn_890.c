// Ticket #890: an @shared-included header that itself defines a
// [[cccc::comptime]] function is now ALSO forwarded via the #890
// allowed-file snapshot (because it defines comptime code), on top of the
// existing @shared replay. Confirms that double-forwarding is benign
// (repeated typedefs/tentative defs are legal C) rather than assumed so.
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
