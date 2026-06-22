// Dynamic module fixture for cc_load_module test (#564).
// No main() — compiled as a .c4d plugin.
// Exports a function-pointer table via a global so the host can call it.

typedef int (*math_fn_t)(int, int);

// The host reads this table after loading the module.
math_fn_t plugin_add;
math_fn_t plugin_mul;

static int do_add(int a, int b) { return a + b; }
static int do_mul(int a, int b) { return a * b; }

// Module init: fill the exported function-pointer table.
void plugin_init(void) {
    plugin_add = do_add;
    plugin_mul = do_mul;
}
