// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: (?=[\s\S]*#include "test_serialize_shared_typedef_1241\.h"[\s\S]*static const MyByte1241 my_table_1241\[4\];)(?=[\s\S]*#include "test_serialize_shared_typedef_1241\.h"[\s\S]*static const struct MyPair1241 my_pairs_1241\[2\];)
// CCCC_REJECT_STDOUT: static const (MyByte1241 my_table_1241|struct MyPair1241 my_pairs_1241)\[[0-9]\][\s\S]*#include "test_serialize_shared_typedef_1241\.h"
//
// Ticket #1241: `cccc -c=generated` exited 0 and wrote C that does not
// compile when a published global's element type came from an
// `#include @shared`-included header -- the auto-generated forward-
// declaration block (the #928 loop, cc_serialize_program) was written
// ahead of the replayed `#include` that supplies the type, for both a
// typedef alias (MyByte1241) and a tagged struct (MyPair1241). Fixed by
// replaying the leading run of plain, unconditional CCCC_EMIT_SOURCE
// directives (including any @shared #include) before the forward-
// declaration block runs, rather than interleaved with it. Asserts the
// #include now precedes both forward declarations; the REJECT pattern
// pins the original (broken) order so a regression is caught either way.
#include @shared "test_serialize_shared_typedef_1241.h"
#include @comptime < stdio.h>

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

[[cccc::comptime]]
void gen_tag_global(void) {
    Type *pair_ty = GetType("MyPair1241");
    Obj  *v = GlobalVar("my_pairs_1241", MakeArray(MakeConst(pair_ty), 2));
    int   data[4] = {1, 2, 3, 4};
    GlobalVarSetInitData(v, data, 16);
    GlobalVarSetStatic(v, 1);
    PublishNodeAt(v, SyntheticToken("my_pairs_1241"));
}
gen_tag_global();

int main(void) {
    return 42;
}
