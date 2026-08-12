// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int v\[n\]
// CCCC_REJECT_STDOUT: unknown type
//
// A VLA declarator must serialize with its length *expression* intact
// (`int v[n]`), not as an incomplete array or an `/* unknown type */`
// comment. TY_VLA's length is a Node*, so serialize_type_decl() reaches the
// expression serializer through the vm handle now carried on
// SerializeContext -- TY_ARRAY's constant length is printed straight into
// the declarator buffer and needs no such thing.
//
// Only the *type* is asserted here (-m does not build/run the output). The
// full round trip -- the ND_VLA_PTR expression case, declaration-hoisting
// order, and the subscript pointer-arithmetic fix -- is covered end-to-end
// by tools/comptime_native_smoke.py's vla_964 case (#964).

int main(void) {
    int n = 4;
    int v[n];
    v[0] = 42;
    return v[0];
}
