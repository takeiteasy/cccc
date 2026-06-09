// Ticket #303: $get_comptime_ptr returns an addressable AST pointer to an
// evaluated comptime variable.

[[cccc::comptime]]
int magic = 42;

struct Config { int width; int height; };

[[cccc::comptime]]
struct Config cfg = { 20, 22 };

[[cccc::comptime(inline)]]
$node_t *magic_ptr(void) {
    return $get_comptime_ptr("magic");
}

[[cccc::comptime(inline)]]
$node_t *cfg_ptr(void) {
    return $get_comptime_ptr("cfg");
}

int main(void) {
    int *m = magic_ptr();
    struct Config *p = cfg_ptr();
    if (*m != 42)
        return 1;
    if (p->width + p->height != 42)
        return 2;
    return 42;
}
