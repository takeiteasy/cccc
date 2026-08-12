// CCCC_FLAGS: -Woverride-init
// CCCC_EXPECT_STDERR: initializer overrides prior initialization of 'i'.*\[-Woverride-init\]
// #961: designation()'s TY_UNION branch had no -Woverride-init check at
// all, so re-designating the same field of a *named* (not anonymous)
// nested union member was silently unwarned, same as the anonymous case
// #960 stumbled onto (test_warning_override_init_anon_member_960.c).

struct S { union { int i; float f; } u; int tag; };

int main(void) {
    struct S s = { .u.i = 1, .tag = 2, .u.i = 42 };
    return s.u.i;
}
