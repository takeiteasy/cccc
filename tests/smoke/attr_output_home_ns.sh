# -E output keeps its meaning when a host compiler reads it: a gnu::packed
# struct stays packed, cccc-only attributes disappear, and a name in a
# namespace it lacks (ignored by cccc) stays ignored.
. "$(dirname "$0")/lib.sh"

cat >in.c <<'EOS'
struct [[gnu::packed]] S { char c; int x; };
_Static_assert(sizeof(struct S) == 5, "packed");
struct [[cccc::packed]] U { char c; int x; };
_Static_assert(sizeof(struct U) == 8, "not packed");
int * [[cccc::single]] p;
[[gnu::cold]] int f(void) { return 1; }
EOS

"$CCCC" --std=c17 -E in.c >out.c || fail "-E failed"
expect_exit 0 cc -std=c17 -fsyntax-only out.c

cat >in2.c <<'EOS2'
@comptime(attribute("tag")) void tag_handler(AttrTarget *t) { (void)t; }
[[nodiscard]] int f(void);
[[maybe_unused]] static int h;
struct [[tag]] S { int x; };
EOS2

"$CCCC" --std=c17 -E in2.c >out2.c || fail "-E failed"
expect_exit 0 cc -std=c17 -Wall -Werror -fsyntax-only out2.c
