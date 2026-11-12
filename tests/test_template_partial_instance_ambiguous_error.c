// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: ambiguous partial specializations of 'Vec'
@template(A, B) struct Pair {
    A a;
    B b;
};
@template(T) struct Vec {
    T v;
};
@template(T) @specialize(Pair @(T, T)) struct Vec {
    T both[2];
};
@template(U) @specialize(Pair @(int, U)) struct Vec {
    U u;
};
Vec @(Pair @(int, int)) v;
