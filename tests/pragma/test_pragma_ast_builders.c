// Test ticket #51: jcc_ast_assign, jcc_ast_member, jcc_ast_funcall builders

#include <reflection.h>

// ---- jcc_ast_assign test -----------------------------------------------
// Macro that returns an assignment expression (target = value).
// Used as: int x; int y = set_var(x, 99);
// After expansion: int y = (x = 99);  => y == 99, x == 99
#pragma macro
JCC_Node *set_var(JCC_Node *target, JCC_Node *value) {
    JCC *vm = jcc_get_vm();
    return jcc_ast_assign(vm, target, value);
}

// ---- jcc_ast_member test -----------------------------------------------
// Macro that returns obj.field (struct member access).
struct Point { int x; int y; };

#pragma macro
JCC_Node *get_x(JCC_Node *pt) {
    JCC *vm = jcc_get_vm();
    return jcc_ast_member(vm, pt, "x");
}

// ---- jcc_ast_funcall test ----------------------------------------------
// Helper function called by the generated funcall node
int triple(int n) { return n * 3; }

// Macro that generates: triple(arg)
#pragma macro
JCC_Node *call_triple(JCC_Node *arg) {
    JCC *vm = jcc_get_vm();
    JCC_Node *callee = jcc_ast_var_ref(vm, "triple");
    JCC_Node *args[1] = { arg };
    return jcc_ast_funcall(vm, callee, args, 1);
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
