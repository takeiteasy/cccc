// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'N' must be the whole array length in a pattern
@template(T) struct Box {
    T v;
};
@template(T, int N) @specialize(T[N + 1]) struct Box {
    T items[N];
};
