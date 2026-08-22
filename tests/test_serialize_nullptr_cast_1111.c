// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: \(void \*\)0.*\(void \*\)np != \(void \*\)0
// CCCC_REJECT_STDOUT: \((nullptr_t|typeof\(nullptr\))\)
//
// #1111: casting TO nullptr_t is not valid C23 syntax even where assignment
// or conversion would be -- real clang rejects every such cast ("cannot cast
// an object of type 'int' to 'nullptr_t'"). The implicit ND_CASTs the type
// checker inserts wherever nullptr meets an assignment or comparison used to
// spell their destination through serialize_type()'s scalar-typedef alias
// lookup as the bundled <stddef.h> name "nullptr_t". Cast destinations now
// always emit "(void *)" instead -- host nullptr_t is typeof(nullptr) ==
// void *, same size/representation, so every assignment/comparison keeps its
// meaning. Declarations of nullptr_t objects keep their typedef name (valid
// C23), so the REJECT guards only parenthesized *cast* spellings.

#include <stddef.h>

int main(void) {
    nullptr_t np = nullptr;
    if (np != nullptr)
        return 1;
    np = nullptr;
    if ((nullptr_t)np != (nullptr_t)0)
        return 2;
    return np == nullptr ? 42 : 3;
}
