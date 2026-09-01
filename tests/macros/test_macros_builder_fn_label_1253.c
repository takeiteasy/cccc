// A builder-API function (MakeFunction + WithFn + FunctionSetBody) whose body
// is an eager Quote() template containing a label, a computed goto through
// `&&label`, and a plain `goto`. cc_resolve_body_label_refs() runs from
// __builtin_ast_function_set_body / __builtin_ast_pop_fn so these resolve for
// a generated function too, not only for a host function walked by
// cc_expand_macros.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("gen_labels", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("{ void *p = &&second;"
                                  "  goto *p;"
                                  "  first: return 1;"
                                  "  second: goto third;"
                                  "  return 2;"
                                  "  third: return 42; }"));
    }
    PublishNode(fn);
}
gen();

int gen_labels(void);

int main(void) {
    return gen_labels();
}
