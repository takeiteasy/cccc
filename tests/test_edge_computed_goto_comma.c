// Tests: computed goto whose target is a comma expression involving function
// calls — goto *expr1, expr2, target; where expr1/expr2 are calls (side
// effects) and the final element is the actual jump destination.
// The comma operator must be parsed as part of the goto expression, not as
// a separate statement. Both puts calls must execute before the jump.
// Previously caused a stack overflow because the goto target evaluated to 0,
// causing JMPI to jump to PC=0 (entry point), re-entering main infinitely.
#include <stdio.h>

int main() {
    void *dest = &&end;
    goto *puts("Hello world"), puts("Goodbye world"), dest;
end:
    return 42;
}
