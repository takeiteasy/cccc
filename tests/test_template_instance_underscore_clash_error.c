// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'Tuple_struct_a_struct_b' is the name of both Tuple@\(struct_a, struct_b\) and Tuple@\(struct_a_struct_b\)
struct a { int x; };
struct b { int x; };
struct a_struct_b { int x; };

@template(Ts...) struct Tuple { Ts... v; };

int main(void) {
    @inst(struct a, struct b) Tuple p;
    @inst(struct a_struct_b) Tuple q;
    return 42;
}
