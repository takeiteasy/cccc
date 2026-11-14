// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: ambiguous partial specializations of 'W' for 'W_struct_P'
@template(Ts...) struct P {
    char k;
};
@template(Ts...) struct W {
    char k;
};
@template(Ts...) @specialize(P @(Ts *...), Ts...) struct W {
    char k[2];
};
@template(Ts...) @specialize(P @(Ts...), Ts...) struct W {
    char k[3];
};
int main(void) {
    @inst(P @()) W w;
    return 42;
}
