// -c=native: a captured #define whose trailing /* comment spans lines is
// replayed whole, and a "/*" inside a string literal does not open one.
#define K 3 /* spans
   two lines */
#define S "/*"
#include <string.h>

int main(void) {
    return (int)strlen("ab") + 40 + K - 3 + (int)strlen(S) - 2;
}
