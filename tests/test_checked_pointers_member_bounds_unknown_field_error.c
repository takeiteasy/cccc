// EXPECT_COMPILE_ERROR
// A struct member's bounds expression naming a field that doesn't exist
// anywhere is an ordinary "undeclared identifier" error from the
// resolve_member_checked_bounds() re-parse (#921) -- assign() runs inside a
// synthetic scope populated only with this struct's own member
// placeholders and whatever real scope struct_members() is nested in, so
// an unknown name fails exactly like an unknown identifier anywhere else.

struct S {
    int * [[cccc::array, cccc::count(not_a_member)]] p;
};

int main(void) {
    struct S s = {(int[3]){1, 2, 3}};
    return s.p[0];
}
