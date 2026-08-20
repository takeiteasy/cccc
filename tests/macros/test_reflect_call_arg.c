// Test $identifier reflect operator: $SomeStruct as a direct call argument
// to a comptime macro.

typedef struct {
    float r;
    float g;
    float b;
} Color;

[[cccc::comptime]]
Node *check_is_struct(Type *ty) {
    if (GetTypeKind(ty) != TK_STRUCT)
        return MakeIntLiteral(1);
    return MakeIntLiteral(42);
}

[[cccc::comptime]]
Node *test_reflect_arg(void) {
    return check_is_struct($Color);
}

int main(void) {
    return test_reflect_arg();
}
