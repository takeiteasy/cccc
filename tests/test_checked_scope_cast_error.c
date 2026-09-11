// EXPECT_COMPILE_ERROR
// Checked regions (#485), v1 ban list item 2: a cast to an unchecked
// pointer type inside a checked region is a compile error, hooked in
// cast() (src/parse_expr.c) rather than new_cast() (src/parse_core.c), which
// is also the implicit-conversion constructor.

extern int puts(const char *);

[[cccc::checked]]
void f(void) {
    int *[[cccc::array, cccc::count(1)]] a = 0;
    puts((char *)a); // explicit cast to an unchecked pointer type
}

int main(void) {
    f();
    return 42;
}
