// __alignof__/_Alignof of a variable or member reports its declared alignment
// (aligned(N), _Alignas), not just its type's; other expressions report the
// type's.

struct S {
    char c __attribute__((aligned(8)));
    char d;
    _Alignas(16) char e;
};
int x __attribute__((aligned(32)));
_Alignas(64) int y;
int (*fp)(void) __attribute__((aligned(64)));
int plain;
int main(void) {
    struct S         s;
    int              l __attribute__((aligned(16)));
    _Alignas(32) int m;
    int              r  = 0;
    r                  += __alignof__(x) == 32;
    r                  += _Alignof(y) == 64;
    r                  += __alignof__(fp) == 64;
    r                  += __alignof__(plain) == 4;
    r                  += __alignof__(s.c) == 8;
    r                  += __alignof__(s.d) == 1;
    r                  += __alignof__(s.e) == 16;
    r                  += __alignof__(l) == 16;
    r                  += __alignof__(m) == 32;
    r                  += __alignof__((x)) == 32;
    r                  += __alignof__(x + 1) == 4;
    return r == 11 ? 42 : r;
}
