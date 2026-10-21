#ifndef COMPTIME_HEADER_1048_SMOKE_H
#define COMPTIME_HEADER_1048_SMOKE_H
typedef int (*plan_fn_1048)(int);
static int          plan_value_1048;
static plan_fn_1048 plan_ptr_1048;

[[cccc::comptime]]
static void set_plan_1048(void) {
    plan_value_1048 = 14 * 3;
}

[[cccc::comptime]]
void generate_result_1048(void) {
    set_plan_1048();
    Obj *fn = MakeFunction("result_1048", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(plan_value_1048)));
}
#endif
