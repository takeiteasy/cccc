// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDERR: uses type 'MyByte1241' from a header that is not included in the generated output
//
// Ticket #1241 follow-up: when a published global's element type comes
// from a typedef reached only via a never-captured route (here
// `#include @comptime`, never replayed into `-c=generated` output), no
// reordering can help -- nothing in the generated file ever declares the
// type. Unlike the ordering bug this file's sibling
// (test_serialize_shared_typedef_1241.c) exercises, cc_serialize_program
// now fails loudly (find_generated_uncaptured_typedef, serialize_type.c)
// instead of silently writing C the host compiler will reject.
#include @comptime "test_serialize_shared_typedef_1241.h"

[[cccc::comptime]]
void gen_typedef_global(void) {
    Type *byte_ty = GetType("MyByte1241");
    Obj  *v = GlobalVar("my_table_1241", MakeArray(MakeConst(byte_ty), 4));
    unsigned char data[4] = {1, 2, 3, 4};
    GlobalVarSetInitData(v, data, 4);
    GlobalVarSetStatic(v, 1);
    PublishNodeAt(v, SyntheticToken("my_table_1241"));
}
gen_typedef_global();

int main(void) {
    return 42;
}
