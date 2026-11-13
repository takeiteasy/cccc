#include <string.h>

#define LINE_BASE 50
#line 50
static int at_50 = __LINE__;
#line LINE_BASE
static int at_macro_50 = __LINE__;
static int next_51     = __LINE__;
# 70 "gnu.c"
static int         at_70   = __LINE__;
static const char *file_70 = __FILE__;
#line 200 "renamed.c"
static const char *file_200 = __FILE__;
static int         at_200   = __LINE__;

int main(void) {
    if (at_50 != 50 || at_macro_50 != 50 || next_51 != 51)
        return 1;
    if (at_70 != 70 || strcmp(file_70, "gnu.c") != 0)
        return 2;
    if (at_200 != 200 + 1 || strcmp(file_200, "renamed.c") != 0)
        return 3;
    return 42;
}
