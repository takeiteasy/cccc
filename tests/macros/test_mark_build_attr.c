// CCCC_FLAGS: --build
// MarkAsBuild registers a programmatically generated function as a [[cccc::build]] entry.

[[cccc::comptime]]
void gen_build_fn(void) {
    Obj *fn = MakeFunction("gen_build_entry", GetType("int"));
    FunctionAddParam(fn, "ctx", MakePointer(GetType("Builder")));
    WithFn(fn) { FunctionSetBody(fn, Quote("return 42;")); }
    PublishNode(fn);
    MarkAsBuild(fn);
}
gen_build_fn();
