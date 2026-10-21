// A struct with only scalar members round-trips under -c=native.

#include <sys/mount.h>

// #1168: type_layout_is_host_owned() used to accept any Type kind, not
// just struct/union/enum -- a bare scalar member/operand (e.g. plain
// `long`) could spuriously same_type_or_origin()-match an unrelated
// from_include *typedef* of the same builtin (reached merely by including
// some header) via the origin-chain pointer-identity walk, judging an
// entirely ordinary, non-host-owned aggregate "host-owned" and
// re-materializing its sizeof textually instead of folding it.

// The ticket's own minimal repro shape: an ordinary user struct with only
// scalar members, nothing from_include anywhere in it.
struct Loc1168 {
    long   a;
    double b;
};

// A genuinely host-owned wrapper (contains a from_include struct member)
// must still re-materialize -- this case must not overcorrect.
struct Wrap1168 {
    struct statfs s;
};

int main(void) {
    unsigned long loc_sz  = sizeof(struct Loc1168);
    unsigned long int_sz  = sizeof(int);
    unsigned long wrap_sz = sizeof(struct Wrap1168);

    if (loc_sz != sizeof(long) + sizeof(double) &&
        loc_sz != sizeof(double) + sizeof(long))
        return 1;
    if (int_sz != sizeof(int))
        return 2;
    if (wrap_sz == 0)
        return 3;

    return 42;
}
