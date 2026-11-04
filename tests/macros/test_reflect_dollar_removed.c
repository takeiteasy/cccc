// EXPECT_COMPILE_ERROR
// `$Name` no longer reflects; it is an undeclared identifier. Use `^^Name`.
typedef struct {
    int x;
} Point;

[[cccc::comptime]]
Node *bad_reflect(void) {
    Type *ty = $Point;
    return MakeIntLiteral(ty ? 1 : 0);
}

int main(void) {
    return bad_reflect();
}
