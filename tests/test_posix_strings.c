#include <strings.h>

int main(void) {
    if (strcasecmp("abc", "ABC") != 0) return 1;
    if (strcasecmp("abc", "abd") >= 0) return 2;
    if (strncasecmp("abc", "AB", 2) != 0) return 3;
    if (strncasecmp("abc", "abd", 2) != 0) return 4;
    return 42;
}
