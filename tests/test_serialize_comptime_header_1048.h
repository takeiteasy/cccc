// #1048 fixture: a header reached only via a plain #include (never routed
// via `#include @comptime "x.h"`) but containing its own
// [[cccc::comptime]] declarations. Under -c=native this header's own text
// is auto-captured and replayed verbatim as a `#include` -- but unlike an
// ordinary header, the comptime declarations' bodies reference
// reflection-API constructs (Obj/MakeFunction/GetType/...) with no
// meaning to a host compiler. The containing file must be recognized as
// cccc-only (never replayed) the moment such a declaration is seen, the
// same way a directive routed with `@comptime` already is (#896) -- there
// was previously no equivalent marking for this attribute form.
#ifndef TEST_SERIALIZE_COMPTIME_HEADER_1048_H
#define TEST_SERIALIZE_COMPTIME_HEADER_1048_H

typedef int (*plan_fn_1048)(int);

static int plan_value_1048;
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
