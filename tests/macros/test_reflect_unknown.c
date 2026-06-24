// EXPECT_COMPILE_ERROR
// Test $identifier reflect operator: $UnknownName must produce a compile-time error.

[[cccc::comptime]]
Node *bad_reflect(void) {
    Type *ty = $UnknownName;
    return MakeIntLiteral(0);
}

int main(void) {
    return bad_reflect();
}
