// Test ticket #51: __jcc_ast_assign, __jcc_ast_member, __jcc_ast_funcall builders

// ---- __jcc_ast_assign test -----------------------------------------------
// Macro that returns an assignment expression (target = value).
// Used as: int x; int y = set_var(x, 99);
// After expansion: int y = (x = 99);  => y == 99, x == 99
[[jcc::macro(inline)]]
_Node *set_var(_Node *target, _Node *value) {
    _VirtualMachine *vm = __jcc_get_vm();
    return __jcc_ast_assign(vm, target, value);
}

// ---- __jcc_ast_member test -----------------------------------------------
// Macro that returns obj.field (struct member access).
struct Point { int x; int y; };

[[jcc::macro(inline)]]
_Node *get_x(_Node *pt) {
    _VirtualMachine *vm = __jcc_get_vm();
    return __jcc_ast_member(vm, pt, "x");
}

// ---- __jcc_ast_funcall test ----------------------------------------------
// Helper function called by the generated funcall node
int triple(int n) { return n * 3; }

// Macro that generates: triple(arg)
[[jcc::macro(inline)]]
_Node *call_triple(_Node *arg) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Node *callee = __jcc_ast_var_ref(vm, "triple");
    _Node *args[1] = { arg };
    return __jcc_ast_funcall(vm, callee, args, 1);
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
