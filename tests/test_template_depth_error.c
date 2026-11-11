// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: template instantiation depth exceeds
@template(T) struct Rec {
    @inst(T *) Rec *next;
};

int main(void) {
    @inst(int) Rec r;
    (void)r;
    return 0;
}
