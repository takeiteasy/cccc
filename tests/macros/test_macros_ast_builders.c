// Test ticket #51: __builtin_ast_assign, __builtin_ast_member, __builtin_ast_funcall builders

// ---- __builtin_ast_assign test -----------------------------------------------
// Macro that returns an assignment expression (target = value).
// Used as: int x; int y = set_var(x, 99);
// After expansion: int y = (x = 99);  => y == 99, x == 99
[[cccc::comptime]]
Node *set_var(Node *target, Node *value) {
    return __builtin_ast_assign(target, value);
}

// ---- __builtin_ast_member test -----------------------------------------------
// Macro that returns obj.field (struct member access).
struct Point { int x; int y; };

[[cccc::comptime]]
Node *get_x(Node *pt) {
    return __builtin_ast_member(pt, "x");
}

// ---- __builtin_ast_funcall test ----------------------------------------------
// Helper function called by the generated funcall node
int triple(int n) { return n * 3; }

// Macro that generates: triple(arg)
[[cccc::comptime]]
Node *call_triple(Node *arg) {
    Node *callee = __builtin_ast_var_ref("triple");
    Node *args[1] = { arg };
    return __builtin_ast_funcall(callee, args, 1);
}

int main(void) {
    // ---- assign ----
    int x = 0;
    int y = set_var(x, 99);   // expands to: y = (x = 99)
    if (y != 99) return 1;
    if (x != 99) return 2;

    // ---- member ----
    struct Point p = { 7, 13 };
    int px = get_x(p);        // expands to: p.x
    if (px != 7) return 3;

    // ---- funcall ----
    int r = call_triple(5);   // expands to: triple(5) == 15
    if (r != 15) return 4;

    return 42;
}
