// EXPECT_COMPILE_ERROR
// #486: 'assume'/'dynamic' are cast-only -- they may only appear in a
// cast's type-name, never on a declarator. declarator() (src/parse_types.c)
// diagnoses this the moment pointers() returns a non-CC_NONE
// checked_cast_kind for a real declaration.

int *[[cccc::array, cccc::count(5), cccc::assume]] gp;

int main(void) {
    return 0;
}
