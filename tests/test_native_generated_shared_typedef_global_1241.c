// A global of a shared header typedef round-trips under -c=native.
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
