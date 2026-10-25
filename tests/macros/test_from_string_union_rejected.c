// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @from_string does not support unions

#include <cccc/derive.h>
#include <cccc/text.h>

@from_string union U {
    int   i;
    float f;
};

int main(void) {
    return 42;
}
