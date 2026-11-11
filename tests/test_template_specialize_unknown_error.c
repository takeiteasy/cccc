// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'Nope' is not a template
@specialize(int) struct Nope {
    int a;
};
