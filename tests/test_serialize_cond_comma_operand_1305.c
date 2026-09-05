// #1305: found via #1132's self-hosting spike (stage (c) -- the linked
// self-hosted `cccc` SIGABRT'd on the first hashmap rehash during
// cc_init()). serialize_expr.c's ND_COND (ternary) case serialized all
// three operands with parent precedence 0, so `serialize_expr` never
// parenthesized a comma- or assignment-expression operand -- but C's
// grammar makes the condition a logical-OR-expression and the else-branch
// a conditional-expression, both tighter than `,` and `=`.
//
// The bundled <assert.h> macro is
//   ((expr) ? (void)0 : (puts("Assertion failed: " #expr), abort()))
// whose else-branch is a comma expression. It serialized as
//   expr ? (void)0 : puts(...) , abort()
// which re-parses as `(expr ? (void)0 : puts(...)) , abort()` -- abort()
// now runs unconditionally, so *every* assert() in the compiled compiler
// fired the moment it was reached, whatever its condition. cccc's own
// source has exactly two assert() call sites (both in hashmap.c's
// rehash()), which is why no existing -c=native test ever caught it: the
// spike is the first serialized program with a live, passing assert() in a
// hot path.
//
// Fixed by passing get_precedence(ND_COND) for the else-branch and
// get_precedence(ND_COND)+1 for the condition, so a looser-binding operand
// in either position is parenthesized (the middle operand stays bare -- it
// is a full `expression` in the grammar and legitimately may be a comma
// expression without parens).
//
// tools/comptime_native_smoke.py's case_assert_ternary_comma_1305 is the
// load-bearing VM-42-to-native-42 proof: pre-fix the native binary aborts
// (exit 134) on a *passing* assert, which no -m shape assertion can see.

#include <assert.h>

// else-branch is a comma expression via the assert() macro
static int checked_add(int a, int b) {
    assert(a >= 0);     // passes -- must NOT abort
    assert(b >= 0);     // passes -- must NOT abort
    int s = a + b;
    assert(s == a + b); // passes -- must NOT abort
    return s;
}

// hand-written ternary whose condition and else-branch are both comma
// expressions -- `(c = ..., ...)` in the condition, `(e = ..., 0)` in the
// else. Pre-fix the dropped parens changed evaluation entirely.
static int cond_and_else_commas(void) {
    int c = 0, e = 0;
    int r = (c = 1, c + 1) ? (c + 10) : (e = 99, e + 1);
    // condition (c=1, c+1) == 2 -> truthy -> then-branch -> 11; e untouched
    return (r == 11 && e == 0) ? 20 : 0;
}

int main(void) {
    int v  = checked_add(10, 12);    // 22
    v     += cond_and_else_commas(); // + 20 -> 42
    return v;
}
