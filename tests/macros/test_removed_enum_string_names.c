// CCCC_EXPECT_STDERR: unknown attribute 'enum_to_string' ignored
// CCCC_EXPECT_STDERR: unknown attribute 'enum_from_string' ignored

#include <cccc/derive.h>

@enum_to_string enum A { X };
@enum_from_string enum B { Y };

int main(void) {
    return 42;
}
