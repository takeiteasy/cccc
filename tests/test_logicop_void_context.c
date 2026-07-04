// CCCC_EXPECT_STDOUT: AOC
// Tests that &&, ||, and ?: side-effects run correctly when the result is
// discarded (expression statement / void context). Regression test for #628.
//
// Commit 4ff58d5 reworked ND_LOGAND/ND_LOGOR/ND_COND to reuse dest_reg as the
// condition scratch, but ND_EXPR_STMT passes REG_ZERO (hardwired zero) as
// dest_reg, so the condition was silently discarded and always read back as 0.
//   &&: jz always taken  -> rhs never ran (ticket bug)
//   ||: jnz never taken  -> rhs always ran (wrong short-circuit)
//   ?:: always took else  branch
#include <stdio.h>

static int t(const char *s) { fputs(s, stdout); return 1; }

int main(int argc) {
    argc          && t("A");  // argc==1 (truthy) -> rhs runs  -> A
    argc          || t("X");  // argc==1 (truthy) -> short-circuit, X suppressed
    (argc - argc) || t("O");  // 0 (falsy)        -> rhs runs  -> O
    argc ? t("C") : t("Y");   // argc truthy      -> then branch -> C
    return 0;
}
