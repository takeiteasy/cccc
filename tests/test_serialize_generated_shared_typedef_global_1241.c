// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: #include "fixtures/shared_typedef_1241\.h"(.|\n)*static const MyByte1241 my_table_1241
// CCCC_HOST_CC: -Itests
// -c=generated output places the shared header include ahead of the global that
// uses its typedef.
#include @shared "fixtures/shared_typedef_1241.h"
#include @comptime < stdio.h>
[[cccc::comptime]]
void gen(void) {
    Type *byte_ty = GetType("MyByte1241");
    Obj  *v = GlobalVar("my_table_1241", MakeArray(MakeConst(byte_ty), 4));
    unsigned char data[4] = {1, 2, 3, 4};
    GlobalVarSetInitData(v, data, 4);
    GlobalVarSetStatic(v, 1);
    PublishNodeAt(v, SyntheticToken("my_table_1241"));
}
gen();

int main(void) {
    return my_table_1241[0] + my_table_1241[1] + my_table_1241[2] +
           my_table_1241[3] + 32;
}
