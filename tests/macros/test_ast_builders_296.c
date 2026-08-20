// Test ticket #296: CompoundLiteral, InitArray, InitStruct AST builders.
// Exercises comptime macros used at both file scope and expression position.

struct CLPoint {
    int x;
    int y;
};

// ---- CompoundLiteral (inline) ----------------------------------------
// Inline macro: current_fn is the caller, so local var allocation works
// directly.
[[cccc::comptime]]
Node *cl_point_x(void) {
    Type *pt = GetType("CLPoint");
    // positional: {10, 20} — x=10, y=20
    Node *lit = CompoundLiteral(pt, MakeIntLiteral(10), MakeIntLiteral(20));
    return MakeMember(lit, "x");
}

[[cccc::comptime]]
Node *cl_point_y(void) {
    Type *pt  = GetType("CLPoint");
    Node *lit = CompoundLiteral(pt, MakeIntLiteral(10), MakeIntLiteral(20));
    return MakeMember(lit, "y");
}

// ---- InitArray (non-inline, via WithFn) ----------------------------
[[cccc::comptime]]
Node *gen_array_fn(void) {
    Type *int_ty = GetType("int");
    Obj  *fn     = MakeFunction("array_elem1", int_ty);
    WithFn(fn) {
        // int[3]{7, 14, 21}[1] == 14
        Node *arr = InitArray(int_ty, MakeIntLiteral(7), MakeIntLiteral(14),
                              MakeIntLiteral(21));
        FunctionSetBody(fn, MakeReturn(MakeSubscript(arr, MakeIntLiteral(1))));
    }
    return MakeIntLiteral(0);
}
gen_array_fn();

// ---- InitStruct (designated, partial) --------------------------------
// Only .x is set; .y should be zero from the ND_MEMZERO.
[[cccc::comptime]]
Node *is_partial_y(void) {
    Type *pt = GetType("CLPoint");
    Node *s =
        InitStruct(pt, (const char *[]){"x"}, (Node *[]){MakeIntLiteral(5)}, 1);
    return MakeMember(s, "y");
}

// Both fields designated: use local arrays to avoid preprocessor comma
// confusion.
[[cccc::comptime]]
Node *is_both_sum(void) {
    Type       *pt       = GetType("CLPoint");
    const char *fields[] = {"x", "y"};
    Node       *vals_a[] = {MakeIntLiteral(3), MakeIntLiteral(4)};
    Node       *s        = InitStruct(pt, fields, vals_a, 2);
    Node       *xv       = MakeMember(s, "x");
    // Fresh instance for y access (each InitStruct call creates its own anon
    // var).
    Node *vals_b[] = {MakeIntLiteral(3), MakeIntLiteral(4)};
    Node *s2       = InitStruct(pt, fields, vals_b, 2);
    Node *yv       = MakeMember(s2, "y");
    return MakeBinary(NK_ADD, xv, yv);
}

// ==========================================================================
// Runtime assertions
// ==========================================================================

int main(void) {
    // CompoundLiteral positional: {10, 20}
    if (cl_point_x() != 10)
        return 1;
    if (cl_point_y() != 20)
        return 2;

    // InitArray: int[3]{7, 14, 21}[1] == 14
    if (array_elem1() != 14)
        return 3;

    // InitStruct partial: {.x=5}.y == 0
    if (is_partial_y() != 0)
        return 4;

    // InitStruct both fields: .x=3, .y=4; sum == 7
    if (is_both_sum() != 7)
        return 5;

    return 42;
}
