// Positive control for #890: the same fixture body as
// test_comptime_include_boundary_890.c, but inlined into a single file
// instead of arriving via #include. Guards against "fixing" the include
// case by accidentally breaking the same-file case.

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

generate_result_890();

int main(void) {
    return result_890();
}
