// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a template instance takes its arguments once
@template(T) struct Vec {
    T *data;
};

int main(void) {
    @inst(int) Vec @(int) v;
    return 0;
}
