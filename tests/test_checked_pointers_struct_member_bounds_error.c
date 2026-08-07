// EXPECT_COMPILE_ERROR
// Bounds on a struct/union member are rejected in v1 (#770/#483): a bounds
// expression naming a sibling field needs member-relative resolution that
// resolve_checked_bounds()'s scope-based re-parse can't do. See
// struct_members() in src/parse.c.

struct S {
    int n;
    int * [[cccc::array, cccc::count(n)]] p;
};

int main(void) {
    struct S s;
    s.n = 3;
    return 42;
}
