// Test ticket #195: $@k unquote-splicing in compound-literal initializer-list position.
// main() returns 42 on success.

struct Point { int x; int y; };
struct Triple { int a; int b; int c; };

// ---- Test 1: splice into a struct compound literal --------------------------
// Builds a two-element chain and splices it as positional initializers for Point.
[[cccc::comptime]]
Node *make_point(Node *px, Node *py) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *chain = __builtin_node_list(vm, (Node*[]){ px, py }, 2);
    return Quote("(struct Point){ $@1 }", chain);
}

int test_struct_splice(void) {
    struct Point p = make_point(10, 32);
    return (p.x == 10 && p.y == 32) ? 1 : 0;
}

// ---- Test 2: splice into an array compound literal --------------------------
// Builds a three-element chain and splices it into int[3].
// The compound literal decays to a pointer; sum all elements.
[[cccc::comptime]]
Node *make_arr3(Node *a, Node *b, Node *c) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *chain = __builtin_node_list(vm, (Node*[]){ a, b, c }, 3);
    return Quote("(int[3]){ $@1 }", chain);
}

int test_array_splice(void) {
    int *p = make_arr3(10, 20, 12);
    return (p[0] == 10 && p[1] == 20 && p[2] == 12) ? 1 : 0;
}

// ---- Test 3: struct splice with expression arguments (not just literals) -----
// Verifies that non-trivial caller expressions are correctly substituted.
[[cccc::comptime]]
Node *make_triple(Node *a, Node *b, Node *c) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *chain = __builtin_node_list(vm, (Node*[]){ a, b, c }, 3);
    return Quote("(struct Triple){ $@1 }", chain);
}

int test_struct_triple(void) {
    int x = 3, y = 5, z = 7;
    struct Triple t = make_triple(x * 2, y + 1, z - 1);
    return (t.a == 6 && t.b == 6 && t.c == 6) ? 1 : 0;
}

// ---- Test 4: scalar $1 combined with a separate splice $@2 ------------------
// Verifies that scalar and splice placeholders coexist in one template.
[[cccc::comptime]]
Node *scale_and_make_point(Node *scale, Node *px, Node *py) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *chain = __builtin_node_list(vm, (Node*[]){ px, py }, 2);
    return Quote("(struct Point){ $@2 }", scale, chain);
}

int test_scalar_and_splice(void) {
    struct Point p = scale_and_make_point(99, 21, 21);
    return (p.x == 21 && p.y == 21) ? 1 : 0;
}

// ---- main -------------------------------------------------------------------

int main(void) {
    if (!test_struct_splice())   return 1;
    if (!test_array_splice())    return 2;
    if (!test_struct_triple())   return 3;
    if (!test_scalar_and_splice()) return 4;

    return 42;
}
