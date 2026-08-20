// Test ticket #304: file-scope compound literals in AST builders.
// CompoundLiteral / InitArray / InitStruct called with current_fn == NULL
// (outside any WithFn block in a file-scope comptime macro) produce a static
// anonymous global var instead of a stack local.

struct FPt {
    int x;
    int y;
};

// ---- CompoundLiteral outside WithFn (current_fn == NULL) ---------------
// The global_pt literal lives in static storage; the function captures it.
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

// ---- InitArray outside WithFn ------------------------------------------
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

// ---- InitStruct outside WithFn, partial (y should be zero) --------------
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

int main(void) {
    if (gvar_cl_x() != 7)
        return 1;
    if (gvar_arr_elem2() != 30)
        return 2;
    if (gvar_struct_x() != 99)
        return 3;
    if (gvar_struct_y() != 0)
        return 4;
    return 42;
}
