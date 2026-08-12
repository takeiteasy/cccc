// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: _Complex double
// CCCC_REJECT_STDOUT: unknown type
//
// A _Complex type must serialize as `_Complex <element>`, derived from the
// type's base (see ty_fcomplex/ty_dcomplex/ty_ldcomplex in type.c). Before
// this, TY_COMPLEX had no case in serialize_type() and fell through to the
// `/* unknown type */` default arm.
//
// Only the *type* is asserted here. The ND_COMPLEX expression case is still
// unserialized, so this program does not yet build under -c=native -- that
// half is tracked separately as part of the serializer node-kind work.

int main(void) {
    _Complex double z = __cccc_cmplx(42.0, 0.0);
    _Complex double w = z;
    return w == z ? 42 : 0;
}
