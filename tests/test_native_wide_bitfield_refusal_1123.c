// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -m
// CCCC_EXPECT_STDERR: cannot serialize a bitfield
//
// #1123's value-level lowering (a statement-expression per operation, an
// emitted __cccc_biK container) does not cover a bitfield whose *declared*
// type is itself a wide (>128-bit) _BitInt -- a bit-field's type must be an
// integer type, and __cccc_biK is a struct, so `__cccc_bi4 f : 193;` is as
// illegal as `struct S f : 193;` would be. Closing that gap needs the whole
// enclosing aggregate rewritten to opaque byte storage (every member access,
// not just this one, going through an offset-based extract/insert), tracked
// as its own follow-up rather than folded into this ticket.
//
// This pins the refusal itself: serialize_type.c's bitfield-member-
// declarator loop must reject this loudly (naming the construct) rather
// than let it fall through to the __cccc_biK spelling, which a host
// compiler rejects with a confusing diagnostic that names neither CCCC nor
// this member (confirmed against both gcc-16 and clang: "bit-field 'f' has
// invalid type") -- or, worse, crash while trying to serialize the RHS of an
// assignment to such a member before ever reaching the struct's own
// definition (the struct isn't always collected into the emitted output by
// the time an assignment through it is serialized).
struct WideBitfieldRefusal1123 {
    _BitInt(256) f : 193;
};

int main(void) {
    struct WideBitfieldRefusal1123 a;
    a.f = 5;
    return (int)a.f;
}
