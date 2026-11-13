# A header's @comptime functions and variables reach every input that includes
# it. An identical copy is one entity; a different copy of the name is an error.
. "$(dirname "$0")/lib.sh"

# Helpers defined in a header that both inputs include and use.
cat >gen.h <<'EOS'
#pragma once
@comptime int base(void) { return 20; }
@comptime void make(const char *name, int extra) {
    Obj *fn = MakeFunction(name, GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(base() + extra)));
}
EOS
cat >a.c <<'EOS'
#include "gen.h"
@comptime void gen_a(void) { make("made_a", 1); }
gen_a();
int from_b(void);
int made_a(void);
int main(void) { return made_a() + from_b(); }
EOS
cat >b.c <<'EOS'
#include "gen.h"
@comptime void gen_b(void) { make("made_b", 0); }
gen_b();
int made_b(void);
int from_b(void) { return made_b(); }
EOS
cat >c.c <<'EOS'
#include "gen.h"
int unused;
EOS
expect_exit 41 "$CCCC" a.c b.c
expect_exit 41 "$CCCC" a.c b.c c.c
expect_exit 0 "$CCCC" -c=native -o shared_out a.c b.c c.c
expect_exit 41 ./shared_out

# Derive attributes in two inputs, with and without the explicit include.
for x in p q; do
    cat >d_$x.c <<EOS
struct S_$x { int v; } @equals;
EOS
done
cat >>d_p.c <<'EOS'
int from_q(void);
int main(void) {
    struct S_p a = {1}, b = {1};
    return 41 + S_p_equals(&a, &b) * 0 + from_q();
}
EOS
cat >>d_q.c <<'EOS'
int from_q(void) {
    struct S_q a = {1}, b = {1};
    return S_q_equals(&a, &b);
}
EOS
expect_exit 42 "$CCCC" d_p.c d_q.c
for x in p q; do
    printf '#include <cccc/derive.h>\n' | cat - d_$x.c >e_$x.c
done
expect_exit 42 "$CCCC" e_p.c e_q.c

# The same function pasted into two inputs.
printf '@comptime int k(void) { return 1; }\nint from_b(void);\nint main(void) { return 41 + from_b(); }\n' >s1.c
printf '@comptime int k(void) { return 1; }\nint from_b(void) { return 1; }\n' >s2.c
expect_exit 42 "$CCCC" s1.c s2.c

# A different function of the same name.
printf '@comptime int k(void) { return 2; }\nint from_b(void) { return 1; }\n' >s3.c
expect_fail "$CCCC" s1.c s3.c
grep -q "comptime function 'k' is already defined differently at s1.c:1" err.txt ||
    fail "no conflicting-function error: $(cat err.txt)"

# A comptime variable shared through a header, and two that disagree.
cat >var.h <<'EOS'
#pragma once
@comptime int cv = 3;
EOS
for x in p q; do
    cat >v_$x.c <<EOS
#include "var.h"
@comptime void gen_$x(void) {
    Obj *fn = MakeFunction("v_$x", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(cv * 7)));
}
gen_$x();
EOS
done
cat >>v_p.c <<'EOS'
int v_q(void);
int main(void) { return v_p() + v_q(); }
EOS
expect_exit 42 "$CCCC" v_p.c v_q.c

printf '@comptime int cv = 5;\nint other;\n' >w1.c
printf '@comptime int cv = 6;\nint main(void) { return 0; }\n' >w2.c
expect_fail "$CCCC" w1.c w2.c
grep -q "comptime variable 'cv' is already defined differently at w1.c:1" err.txt ||
    fail "no conflicting-variable error: $(cat err.txt)"

# A struct derived in a shared header is one definition per program.
cat >derived.h <<'EOS'
#pragma once
struct M { int a; char *name; } @equals @hash @getters @setters @ctor @to_string;
EOS
cat >m_p.c <<'EOS'
#include <stdio.h>
#include <cccc/derive.h>
#include "derived.h"
int from_q(void);
int main(void) {
    struct M m = {0};
    set_a(&m, 3);
    return 39 + get_a(&m) + from_q();
}
EOS
cat >m_q.c <<'EOS'
#include <stdio.h>
#include <cccc/derive.h>
#include "derived.h"
int from_q(void) {
    struct M m = {0};
    return M_equals(&m, &m) * 0;
}
EOS
printf '#include <stdio.h>\n#include <cccc/derive.h>\n#include "derived.h"\nint unused_r;\n' >m_r.c
expect_exit 42 "$CCCC" m_p.c m_q.c
expect_exit 42 "$CCCC" m_p.c m_q.c m_r.c

# Two different structs of one name still collide.
printf '#include <cccc/derive.h>\nstruct P { int x; } @equals;\nint main(void) { return 0; }\n' >o_p.c
printf '#include <cccc/derive.h>\nstruct P { long y; } @equals;\nint other;\n' >o_q.c
expect_fail "$CCCC" o_p.c o_q.c
grep -q "redefinition of 'P_equals'" err.txt ||
    fail "no duplicate-derive error: $(cat err.txt)"

# -c=native does not replay a header that holds a derive attribute or includes
# <cccc/derive.h>; the struct and its functions come from the generated C.
expect_exit 0 "$CCCC" -c=native -o derived_out m_p.c m_q.c m_r.c
expect_exit 42 ./derived_out
cat >own.h <<'EOS'
#pragma once
#include <cccc/derive.h>
struct Own { int v; } @equals;
EOS
cat >n_p.c <<'EOS'
#include "own.h"
int from_q(void);
int main(void) {
    struct Own a = {1}, b = {1};
    return 41 + Own_equals(&a, &b) * 0 + from_q();
}
EOS
cat >n_q.c <<'EOS'
#include "own.h"
int from_q(void) { return 1; }
EOS
expect_exit 42 "$CCCC" n_p.c n_q.c
expect_exit 0 "$CCCC" -c=native -o own_one n_p.c n_q.c
expect_exit 42 ./own_one
cat >n_s.c <<'EOS'
#include "own.h"
int main(void) {
    struct Own a = {1}, b = {1};
    return 41 + Own_equals(&a, &b);
}
EOS
expect_exit 0 "$CCCC" -c=native -o own_solo n_s.c
expect_exit 42 ./own_solo
