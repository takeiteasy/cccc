// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -m
// CCCC_EXPECT_STDERR: an anonymous struct/union member
//
// An anonymous member of an opaque-storage aggregate would need its own
// offset-based lowering.
struct WithAnon {
    _BitInt(256) f : 100;
    struct {
        int a;
    };
};

int main(void) {
    struct WithAnon s;
    s.a = 1;
    return s.a;
}
