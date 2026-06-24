// Ticket #303: GetComptimePtr returns an addressable AST pointer to an
// evaluated comptime variable.

[[cccc::comptime]]
int magic = 42;

struct Config { int width; int height; };

[[cccc::comptime]]
struct Config cfg = { 20, 22 };

[[cccc::comptime]]
Node *magic_ptr(void) {
    return GetComptimePtr("magic");
}

[[cccc::comptime]]
Node *cfg_ptr(void) {
    return GetComptimePtr("cfg");
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
