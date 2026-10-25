// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @serialize needs #include <string.h>

#include <cccc/derive.h>
#include <stdlib.h>

@serialize struct Item {
    char *s @owned;
};

int main(void) {
    return 42;
}
