// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @from_json: two owned arrays share the length member 'n'

#include <cccc/derive.h>
#include <cccc/json.h>

@from_json struct Pair {
    int  n;
    int *a @owned("n");
    int *b @owned("n");
};

int main(void) {
    return 42;
}
