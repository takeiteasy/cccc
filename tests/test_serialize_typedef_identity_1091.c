// Expected return: 42
// #1091: -c=native's serializer collapsed structurally-identical-but-
// nominally-distinct typedef'd structs into one printed type, keyed off
// same_type_or_origin()'s deliberately structural (member-wise) fallback --
// load-bearing for #1006/#1046's own same-declaration-reparsed dedup, but
// wrong here: two UNRELATED typedefs sharing a shape are still two
// different C types. Found verifying #1090's div/ldiv/lldiv fix (a native
// round-trip using both ldiv_t and lldiv_t in one TU -- long and long long
// share a representation on every 64-bit target this project supports, so
// CCCC's own ldiv_t/lldiv_t are byte-identical).
//
// Three symptoms, all fixed together in find_typedef_name()/find_tag_name()
// (identity, via find_typedef_name_exact()'s ->origin-chain walk, tried
// before the structural fallback) and the ctx->defs/ctx->emitted_defs
// dedup (type_vec_push_nominal(), gated on nominally_distinct_typedefs()):
//
//   1. Two tagless typedefs of identically-shaped structs (Pair/Span below)
//      -- pre-fix, only the second-collected one's body was ever printed,
//      and every use of the first was mis-spelled with the second's name.
//   2. A tagless typedef alongside a same-shaped TAGGED struct (Tagless vs
//      struct Tag) -- pre-fix, the tagless one was spelled `struct Tag`
//      (and its own standalone body sometimes duplicated).
//   3. ldiv_t/lldiv_t themselves, exercised end to end.
//
// All four aggregates are used by value (assigned from a same-named local
// function's return, matching the shape #1090's own repro needed) so a
// pre-fix build fails outright under -c=native (redefinition or "assigning
// to X from incompatible type Y" from the host compiler), not just prints
// wrong text -- this is the round-trip proof, see tools/
// comptime_native_smoke.py case 129 for the -m text assertions.

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
