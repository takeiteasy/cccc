// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int main\(void\) \{\nstruct S \{[\s\S]*static struct S __cccc_buf_[0-9]+\[4\];
// CCCC_REJECT_STDOUT: static struct \{
//
// A static whose type is declared inside the function is defined inside that
// function, after the type, instead of at file scope where the type is not
// visible.

int main(void) {
    static struct S {
        int   loc;
        void *fn;
    } buf[4];
    buf[0].loc = 42;
    return buf[0].loc;
}
