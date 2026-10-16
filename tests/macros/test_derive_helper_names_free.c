// derive.h's helper names are only reserved once an attribute from it is used:
// a program with no such attribute may define a comptime helper of the same
// name.

@comptime int __builtin_attr_type_name(int x) {
    return x;
}

@comptime Node *forty_two(void) {
    return MakeIntLiteral(__builtin_attr_type_name(42));
}

int main(void) {
    return forty_two();
}
