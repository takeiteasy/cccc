// Regression (#581): when the LHS of an assignment has a function call in its
// address/index expression, codegen evaluates the RHS first into a caller-saved
// temp, then computes the LHS address. The call inside that address computation
// clobbers every caller-saved temp at runtime, destroying the RHS value (and,
// for struct assignment, the RHS source address) before the store. The fix
// spills the held value across the address computation.

#include <string.h>

typedef struct { int a, b, c; } S;

S items[4];
int two(void) { return 2; }

int main(void) {
    // Scalar: RHS value 's' held across the strlen() call in the LHS index.
    char form[8];
    strcpy(form, "%f");
    form[strlen(form) - 1] = 's';
    if (strcmp(form, "%s") != 0) return 1;

    // Struct: RHS source address held across the call in the LHS index.
    S v = {10, 20, 30};
    items[two()] = v;
    if (items[2].a != 10 || items[2].b != 20 || items[2].c != 30) return 2;

    return 42;
}
