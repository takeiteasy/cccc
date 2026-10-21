// captured #define with a multi-line trailing /* comment is replayed on one
// line, and a "/*" string does not open one.
#define K                                                                      \
    3 /* spans                                                                 \
two lines */
#define S "/*"
#include <string.h>
int main(void) {
    return (int)strlen("ab") + 40 + K - 3 + (int)strlen(S) - 2;
}
