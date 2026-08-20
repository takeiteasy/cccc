// Test $identifier reflect operator: $fn_name yields a non-NULL Obj*

int helper(int x) {
    return x + 1;
}

[[cccc::comptime]]
Node *reflect_fn(void) {
    Obj *obj = $helper;
    if (!obj)
        return MakeIntLiteral(1);
    return MakeIntLiteral(42);
}

int main(void) {
    return reflect_fn();
}
