// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @to_json: owned member 'Outer.in' needs Inner_json_write; put @to_json on 'Inner', declared before 'Outer'

#include <cccc/derive.h>
#include <cccc/json.h>

struct Inner {
    char *s @owned;
};

@to_json struct Outer {
    struct Inner *in @owned;
};

int main(void) {
    return 42;
}
