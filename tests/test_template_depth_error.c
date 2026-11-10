// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: template instantiation depth exceeds
@template(T) struct Rec { @inst(Rec, T *) *next; };

int main(void) {
    @inst(Rec, int) r;
    (void)r;
    return 0;
}
