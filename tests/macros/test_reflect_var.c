// Test $identifier reflect operator: $some_var yields a non-NULL Obj*

int counter = 0;

[[cccc::comptime]]
Node *reflect_var(void) {
    Obj *obj = $counter;
    if (!obj)
        return MakeIntLiteral(1);
    return MakeIntLiteral(42);
}

int main(void) {
    return reflect_var();
}
