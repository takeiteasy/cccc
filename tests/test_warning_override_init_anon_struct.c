// CCCC_FLAGS: -Woverride-init
// CCCC_EXPECT_STDERR: initializer overrides prior initialization of
// 'a'.*\[-Woverride-init\] Regression guard for the #961 refactor: a field
// reached through an anonymous *struct* member already warned correctly before
// #961 (the ticket's premise that anonymity itself was the gap was wrong -- see
// test_warning_override_init_anon_member_960.c's comment). This pins that
// existing behaviour so the is_anon_aggregate_member()/warn_override_init_
// member() factoring in src/parse.c doesn't regress it.

struct S {
    struct {
        int a;
        int b;
    };
    int tag;
};

int main(void) {
    struct S s = {.a = 1, .tag = 2, .a = 42};
    return s.a;
}
