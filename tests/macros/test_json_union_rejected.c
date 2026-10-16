// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @to_json does not support unions

#include <cccc/derive.h>
#include <cccc/json.h>

@to_json
union Bits {
    int   i;
    float f;
};

int main(void) {
    return 42;
}
