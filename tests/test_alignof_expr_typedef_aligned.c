// _Alignof/__alignof__ of an expression whose type is a typedef with
// aligned(N) reports N; a result of any other type reports that type's own.
typedef int    i64 __attribute__((aligned(64)));
typedef long   l64 __attribute__((aligned(64)));
typedef double d64 __attribute__((aligned(64)));
typedef float  f32 __attribute__((aligned(32)));
typedef short  s64 __attribute__((aligned(64)));

struct S {
    i64 m;
};

i64       g, h, *p = &g;
l64       L;
d64       D;
f32       F;
s64       S;
int       x;
const d64 CD = 1;
struct S  s, *sp = &s;
double    dd;

int main(void) {
    int r  = 0;
    r     += __alignof__(*p) == 64;
    r     += __alignof__(p[1]) == 64;
    r     += __alignof__(*&g) == 64;
    r     += __alignof__(s.m) == 64;
    r     += __alignof__(sp->m) == 64;
    r     += __alignof__((i64){0}) == 64;
    r     += _Alignof(*p) == 64;
    r     += __alignof__(g = 1) == 64;
    r     += __alignof__(g += 1) == 64;
    r     += __alignof__(g++) == 64;
    r     += __alignof__((0, g)) == 64;
    r     += __alignof__(x ? g : h) == 64;
    r     += __alignof__(-g) == 64;
    r     += __alignof__(+g) == 64;
    r     += __alignof__(~g) == 64;
    r     += __alignof__(g << 1) == 64;
    r     += __alignof__(g + g) == 64;
    r     += __alignof__(g * h) == 64;
    r     += __alignof__(L + 1) == 64;
    r     += __alignof__(L + x) == 64;
    r     += __alignof__(D + 1) == 64;
    r     += __alignof__(D + F) == 64;
    r     += __alignof__(F + F) == 32;
    r     += __alignof__(F + 1) == 32;
    // Two requests on one type: gcc takes the right operand's.
    r += __alignof__(x + g) == 64;
    r += __alignof__(g + x) == 4;
    // Qualifiers of the operand do not leak into the result type.
    __typeof__(CD + 1) q  = 0;
    q                     = 1;
    r                    += q == 1;
    // Result is a different type: plain.
    r += __alignof__(g + 1) == 4;
    r += __alignof__(x ? g : x) == 4;
    r += __alignof__(g == g) == 4;
    r += __alignof__(!g) == 4;
    r += __alignof__(D + dd) == 8;
    r += __alignof__(F + dd) == 8;
    r += __alignof__(S + S) == 4;
    r += __alignof__(+S) == 4;
    r += __alignof__(-S) == 4;
    r += __alignof__(~S) == 4;
    r += __alignof__(S << 1) == 4;
    r += __alignof__((i64)x) == 4; // gcc
    // The qualifying expression's own alignment stays 64.
    r += __alignof__(S) == 64;
    return r == 40 ? 42 : r;
}
