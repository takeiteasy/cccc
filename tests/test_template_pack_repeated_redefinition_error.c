// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of partial specialization of 'W'
@template(Ts...) struct P {
    char k;
};
@template(Ts...) struct W {
    char k;
};
@template(Ts...) @specialize(P @(Ts...), Ts...) struct W {
    char k[3];
};
@template(Us...) @specialize(P @(Us...), Us...) struct W {
    char k[4];
};
int main(void) {
    return 42;
}
