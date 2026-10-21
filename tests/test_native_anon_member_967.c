// -c=native, member access through an anonymous struct/union member (e.g. s.i
// where i belongs to an unnamed nested struct) no longer emits the invalid
// `s./* unknown */.i` -- ND_MEMBER's else arm now leaves the anonymous link
// transparent instead of printing a placeholder comment

struct S {
    struct {
        int i;
    };
    int tag;
};

int main(void) {
    struct S s = {.i = 1, .tag = 2};
    s.i        = 42;
    return s.i == 42 && s.tag == 2 ? 42 : 1;
}
