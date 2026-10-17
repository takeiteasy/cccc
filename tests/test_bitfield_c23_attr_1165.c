// A C23 [[...]] attribute after a bit-field's width is accepted (gcc/clang
// reject it); it is the spelling @name rewrites to.

struct C23AttrBitfield {
    char a;
    int  b : 5 [[gnu::aligned(4)]];
    int  c;
};

int main(void) {
    struct C23AttrBitfield s = {40, 1, 1};
    return s.a + s.b + s.c;
}
