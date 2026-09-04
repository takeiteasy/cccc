// Expected return: 42
// #1283: the -c=native / -m serializer's type-name provenance/dedup lookups
// (find_tag_name / find_typedef_name / type_has_tag_for_owner /
// type_vec_*_nominal / ...) used to linear-scan the whole tag/typedef/seen
// registry calling same_type_or_origin() per entry -- an O(n) probe run O(n)
// times, the self-hosting-spike performance wall. They now narrow each probe
// through a hash-keyed candidate index (by origin-chain root, by struct/union
// tag, by depth-0 structural fingerprint), with same_type_or_origin() still
// the final arbiter on the candidate set.
//
// This is the round-trip regression that the narrowing preserves every arm's
// result. It exercises, in one TU that must compile and run to 42 under
// -c=native:
//   - a self-referential struct (same_type_or_origin's TY_PTR cycle-breaker)
//   - a struct with a function-pointer member (the TY_FUNC arm)
//   - an array-of-aggregate member (the TY_ARRAY arm)
//   - a tagged struct next to a structurally-identical tagless typedef
//     (must stay TWO distinct printed types -- by_tag vs by_fp)
//   - two unrelated tagless typedefs of the same shape (must both be printed)
//   - a tagged enum used by value (by_fp enumerator-list arm)
// A regression in the index (a missed candidate) makes the host compiler
// reject the emitted C -- redefinition, or assignment from an incompatible
// type -- so this fails the build, not just prints wrong text.
//
// CCCC_TYPE_INDEX_VERIFY=1 additionally cross-checks every probe against a
// full linear scan; the whole suite is run that way when the index changes.

struct node {
    int          key;
    struct node *next;
    struct node *prev;
};

typedef struct ops {
    long (*apply)(struct node *n, long acc);
    int (*cmp)(const struct node *a, const struct node *b);
} ops;

typedef struct bundle {
    struct node      head;
    ops              vt;
    struct {
        int cells[4];
    } grid[3];
} bundle;

struct Tag {
    int a, b;
};
typedef struct {
    int a, b;
} Tagless;

typedef struct {
    long x, y;
} Pair;
typedef struct {
    long x, y;
} Span;

enum color { RED, GREEN = 7, BLUE };

static long sum_list(struct node *n, long acc) {
    for (; n; n = n->next)
        acc += n->key;
    return acc;
}
static int node_cmp(const struct node *a, const struct node *b) {
    return a->key - b->key;
}

static Pair mk_pair(void) {
    Pair p = {.x = 1, .y = 2};
    return p;
}
static Span mk_span(void) {
    Span s = {.x = 3, .y = 4};
    return s;
}
static Tagless mk_tagless(void) {
    Tagless t = {.a = 5, .b = 6};
    return t;
}

int main(void) {
    struct node c = {3, 0, 0};
    struct node b = {2, &c, 0};
    struct node a = {1, &b, 0};

    bundle bl          = {0};
    bl.head            = a;
    bl.vt.apply        = sum_list;
    bl.vt.cmp          = node_cmp;
    bl.grid[1].cells[2] = 9;

    long s = bl.vt.apply(&bl.head, 0); // 1 + 2 + 3
    if (s != 6)
        return 1;
    if (bl.vt.cmp(&c, &a) <= 0)
        return 2;
    if (bl.grid[1].cells[2] != 9)
        return 3;

    struct Tag tg = {.a = 4, .b = 4};
    Tagless    tl = mk_tagless();
    if (tg.a + tg.b != 8 || tl.a + tl.b != 11)
        return 4;

    Pair p = mk_pair();
    Span sp = mk_span();
    if (p.x + p.y != 3 || sp.x + sp.y != 7)
        return 5;

    enum color col = GREEN;
    if ((int)col != 7)
        return 6;

    long total = s + (tg.a + tg.b) + (tl.a + tl.b) + (p.x + p.y) +
                 (sp.x + sp.y) + (int)col + bl.grid[1].cells[2];
    // 6 + 8 + 11 + 3 + 7 + 7 + 9 == 51 -> 51 - 9 == 42
    return (int)(total - 9);
}
