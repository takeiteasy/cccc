// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: static struct __cccc_anon[0-9]+ __cccc_buf_[0-9]+\[4\];
// CCCC_REJECT_STDOUT: \(struct \{
//
// A tagless struct declared in a function gets a synthesized tag, so its
// static is defined once, under that tag.

int main(void) {
    static struct {
        int   loc;
        void *fn;
    } buf[4];
    buf[0].loc = 42;
    return buf[0].loc;
}
