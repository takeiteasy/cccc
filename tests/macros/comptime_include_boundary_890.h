#ifndef COMPTIME_INCLUDE_BOUNDARY_890_H
#define COMPTIME_INCLUDE_BOUNDARY_890_H
// Fixture for #890: a comptime function must see file-scope declarations
// written alongside it, whether the call site is in this file or arrives
// via #include from a driver file.

typedef int (*plan_fn)(int);

static int plan_value;
static plan_fn plan_ptr;

[[cccc::comptime]]
static void set_plan(void) {
    plan_value = 14 * 3;
}

[[cccc::comptime]]
void generate_result_890(void) {
    set_plan();
    Obj *fn = MakeFunction("result_890", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(plan_value)));
}

#endif
