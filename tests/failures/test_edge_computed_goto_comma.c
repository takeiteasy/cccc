// Tests: computed goto whose target is a comma expression involving function
// calls — goto *expr1, expr2, func; where expr1/expr2 are calls returning
// a void pointer and the final element is a function (decays to pointer).
// Godbolt: exit 16, stdout "Hello world\nGoodbye world"
// CCCC: stack overflow (exit 255) — infinite recursion in computed-goto evaluation.
#include <stdio.h>
#include <stdlib.h>

int main() {

  goto *puts("Hello world"), puts("Goodbye world"), exit;

}
