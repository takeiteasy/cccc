// EXPECT_COMPILE_ERROR
//
// #1165: a C23 [[...]] attribute after a bit-field's width is a syntax
// error, both in cccc and in gcc/clang -- their constant-expression parser
// grabs the leading `[` at this position before ever reaching the
// attribute list (verified against gcc-16/clang: both reject this with
// "bit-field 'b' width not an integer constant" / "expected expression").
// cccc's own struct_members() (parse_types.c) deliberately parses only a
// GNU __attribute__(...) list after a bit-field's width, not a C23
// [[...]] one -- this pins that as intended behavior, not an accidental
// gap.

struct C23AttrBitfield1165 {
    char a;
    int  b : 5 [[deprecated]];
    int  c;
};

int main(void) {
    return 0;
}
