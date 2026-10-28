// CCCC_FLAGS: --std=c17 -Wpedantic
// CCCC_EXPECT_STDERR: 'struct S' declared in 'for' loop initial declaration
// CCCC_EXPECT_STDERR: 'union U' declared in 'for' loop initial declaration
// CCCC_EXPECT_STDERR: 'enum E' declared in 'for' loop initial declaration
// CCCC_EXPECT_STDERR: 'struct N' declared in 'for' loop initial declaration
// CCCC_EXPECT_STDERR: declaration of non-variable 'A' in 'for' loop initial declaration
// CCCC_REJECT_STDERR: 'struct Q' declared
// CCCC_REJECT_STDERR: 'struct <anon>'
//
// A tag first declared in a for-init warns before C23; an anonymous tag and
// a reference to an existing tag do not.
struct Q {
    int a;
};

int main(void) {
    int r = 0;
    for (struct S { int a; } s = {1}; s.a < 1; s.a++)
        r++;
    for (union U { int a; } *u = 0; u;)
        r++;
    for (enum E{A} e = A; e != A;)
        r++;
    for (struct N *p = 0; p;)
        r++;
    for (struct { int a; } t = {1}; t.a < 1; t.a++)
        r++;
    for (struct Q *q = 0; q;)
        r++;
    return r == 0 ? 42 : 1;
}
