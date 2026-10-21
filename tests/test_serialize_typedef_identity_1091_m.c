// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: \} Pair;
// CCCC_EXPECT_STDOUT: \} Span;
// CCCC_REJECT_STDOUT: struct Tag A
// CCCC_REJECT_STDOUT: struct Tag Tagless
// -m output keeps nominally distinct tagless typedefs separate.
typedef struct {
    int a, b;
} Pair;
typedef struct {
    int a, b;
} Span;
struct Tag {
    int v;
};
typedef struct {
    int v;
} Tagless;
typedef struct {
    long quot, rem;
} ldiv_t;
typedef struct {
    long long quot, rem;
} lldiv_t;
static Pair mk_pair(void) {
    Pair p;
    p.a = 1;
    p.b = 2;
    return p;
}
static Span mk_span(void) {
    Span s;
    s.a = 3;
    s.b = 4;
    return s;
}
static Tagless mk_tagless(void) {
    Tagless t;
    t.v = 5;
    return t;
}
int main(void) {
    Pair p = mk_pair();
    Span s = mk_span();
    if (p.a + p.b != 3 || s.a + s.b != 7)
        return 1;
    struct Tag tag;
    tag.v     = 6;
    Tagless t = mk_tagless();
    if (tag.v != 6 || t.v != 5)
        return 2;
    ldiv_t  l  = {.quot = -3, .rem = -2};
    lldiv_t ll = {.quot = -3, .rem = 2};
    if (l.quot != -3 || l.rem != -2 || ll.quot != -3 || ll.rem != 2)
        return 3;
    int total = (p.a + p.b) + (s.a + s.b) + tag.v + t.v +
                (int)(l.quot + l.rem) + (int)(ll.quot + ll.rem);
    return total == 15 ? 42 : 4;
}
