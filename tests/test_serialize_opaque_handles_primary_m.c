// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct Gamma
// CCCC_EXPECT_STDOUT: struct Delta
// CCCC_REJECT_STDOUT: AttrTarget
// Two opaque typedefs in the primary file stay distinct in -m output.
typedef struct Gamma Gamma;
typedef struct Delta Delta;
Gamma *mk_g(void);
Delta *mk_d(void);
[[cccc::comptime]]
Node *check(void) {
    return MakeIntLiteral(0);
}
int main(void) {
    Gamma *g = mk_g();
    Delta *d = mk_d();
    (void)g;
    (void)d;
    return check();
}
