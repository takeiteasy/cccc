// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: member 'Ref.p' is a pointer; mark it @owned or @to_string_skip

#include <cccc/derive.h>
#include <cccc/text.h>

@from_string struct Ref {
    int *p;
};

int main(void) {
    return 42;
}
