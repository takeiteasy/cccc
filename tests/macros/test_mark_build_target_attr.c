// CCCC_FLAGS: --build --build-dry-run --build-target=gen_target
// CCCC_EXPECT_STDOUT: gen_app
// MarkAsBuildTarget registers a programmatically generated function as a
// [[cccc::build_target]] factory, selectable via --build-target.

typedef struct BuildTarget BuildTarget;
typedef struct Builder     Builder;
BuildTarget *__builtin_build_executable(Builder *ctx, const char *name);

[[cccc::comptime]]
void gen_bt_fn(void) {
    Type *bt_ty      = GetType("BuildTarget");
    Type *builder_ty = GetType("Builder");
    Obj  *fn         = MakeFunction("gen_target", MakePointer(bt_ty));
    FunctionAddParam(fn, "ctx", MakePointer(builder_ty));
    WithFn(fn) {
        Node *ctx_ref = MakeParamRef(fn, "ctx");
        Node *call =
            Quote("__builtin_build_executable($1, \"gen_app\")", ctx_ref);
        FunctionSetBody(fn, MakeReturn(call));
    }
    PublishNode(fn);
    MarkAsBuildTarget(fn, "native");
}
gen_bt_fn();
