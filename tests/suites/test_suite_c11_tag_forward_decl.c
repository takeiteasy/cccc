// CCCC_FLAGS: --testing
// C11 6.7.2.3p7: a lone `struct S;` / `union S;` declares S in the current
// scope, shadowing an outer tag of any kind.

#pragma cccc suite begin "c11_tag_forward_decl"

union Outer {
    int  a;
    char pad[16];
};

struct Wrapper {
    struct Outer2;
    int b;
};

[[cccc::test]]
void test_block_scope_shadows_outer_tag(void) {
    struct Outer;
    struct Outer {
        char c;
    } *p = 0;
    (void)p;
    AssertEq((int)sizeof(struct Outer), 1);
}

[[cccc::test]]
void test_outer_tag_intact_after_block(void) {
    {
        struct Outer;
    }
    AssertEq((int)sizeof(union Outer), 16);
}

[[cccc::test]]
void test_repeated_forward_decl_in_same_scope(void) {
    struct Later;
    struct Later;
    struct Later *p = 0;
    AssertTrue(p == 0);
}

[[cccc::test]]
void test_forward_decl_after_definition_is_noop(void) {
    struct Done {
        int v;
    };
    struct Done;
    struct Done d = {.v = 42};
    AssertEq(d.v, 42);
}

[[cccc::test]]
void test_member_list_declaration_still_works(void) {
    struct Wrapper w = {.b = 5};
    AssertEq(w.b, 5);
}

#pragma cccc suite end
