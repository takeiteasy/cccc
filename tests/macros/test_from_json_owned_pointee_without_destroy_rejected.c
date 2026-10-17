// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @from_json: owned member 'Outer.in' needs Inner_destroy

#include <cccc/derive.h>
#include <cccc/json.h>
#include <stdlib.h>

@from_json struct Inner {
    char *s @owned;
};

@from_json struct Outer {
    struct Inner *in @owned;
};

int main(void) {
    return 42;
}
