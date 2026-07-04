// Test $identifier reflect operator: $SomeStruct yields a Type*
// Verified by passing it to GetTypeKind and checking TK_STRUCT.

typedef struct { int x; int y; } Point;

[[cccc::comptime]]
Node *reflect_type_kind(void) {
    Type *ty = $Point;
    if (GetTypeKind(ty) != TK_STRUCT)
        return MakeIntLiteral(1);
    return MakeIntLiteral(42);
}

int main(void) {
    return reflect_type_kind();
}
