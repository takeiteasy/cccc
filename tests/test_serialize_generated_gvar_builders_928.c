// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: gvar_cl_x
// CCCC_EXPECT_STDOUT: static struct FPt __cccc_[A-Za-z0-9_]+;
// CCCC_EXPECT_STDOUT: static int __cccc_[A-Za-z0-9_]+\[3\];
// CCCC_REJECT_STDOUT: \.L\.\.
// CCCC_HOST_CC
// -c=generated output for file-scope CompoundLiteral/InitArray/InitStruct
// globals is valid, compilable C.
struct FPt {
    int x;
    int y;
};

[[cccc::comptime]]
Node *gen_gvar_cl(void) {
    Type *pt_ty  = GetType("FPt");
    Type *int_ty = GetType("int");
    Node *gpt = CompoundLiteral(pt_ty, MakeIntLiteral(7), MakeIntLiteral(13));
    Obj  *fn  = MakeFunction("gvar_cl_x", int_ty);
    WithFn(fn) {
        FunctionSetBody(fn, MakeReturn(MakeMember(gpt, "x")));
    }
    return MakeIntLiteral(0);
}
gen_gvar_cl();

[[cccc::comptime]]
Node *gen_gvar_arr(void) {
    Type *int_ty = GetType("int");
    Node *garr   = InitArray(int_ty, MakeIntLiteral(10), MakeIntLiteral(20),
                             MakeIntLiteral(30));
    Obj  *fn     = MakeFunction("gvar_arr_elem2", int_ty);
    WithFn(fn) {
        FunctionSetBody(fn, MakeReturn(MakeSubscript(garr, MakeIntLiteral(2))));
    }
    return MakeIntLiteral(0);
}
gen_gvar_arr();

[[cccc::comptime]]
Node *gen_gvar_struct(void) {
    Type       *pt_ty  = GetType("FPt");
    Type       *int_ty = GetType("int");
    const char *flds[] = {"x"};
    Node       *vals[] = {MakeIntLiteral(99)};
    Node       *gs     = InitStruct(pt_ty, flds, vals, 1);
    Obj        *fn_x   = MakeFunction("gvar_struct_x", int_ty);
    WithFn(fn_x) {
        FunctionSetBody(fn_x, MakeReturn(MakeMember(gs, "x")));
    }
    Obj *fn_y = MakeFunction("gvar_struct_y", int_ty);
    WithFn(fn_y) {
        FunctionSetBody(fn_y, MakeReturn(MakeMember(gs, "y")));
    }
    return MakeIntLiteral(0);
}
gen_gvar_struct();
