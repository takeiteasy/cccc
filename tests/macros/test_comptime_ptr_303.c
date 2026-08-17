// Ticket #303: GetComptimePtr returns an addressable AST pointer to an
// evaluated comptime variable.
//
// #1049: magic/cfg's values used to be chosen so the two shadow Objs
// aliasing the same (never-allocated) address -- the bug this test exists
// to catch -- happened to still sum to 42: both `*m` and `p->width +
// p->height` read data_seg[0] (wherever the first comptime var, "magic",
// landed), which held 42 either way. Values below are distinct from each
// other and from 42 in every field, and the pointers themselves are
// checked for distinctness, so a regression back to shared-address aliasing
// fails loudly instead of coincidentally passing.

[[cccc::comptime]]
int magic = 17;

struct Config { int width; int height; };

[[cccc::comptime]]
struct Config cfg = { 9, 16 };

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
    if ((void *)m == (void *)p)
        return 1;
    if (*m != 17)
        return 2;
    if (p->width != 9 || p->height != 16)
        return 3;
    return 42;
}
