// MakeVarRef() of an enum constant: anonymous, tagged, with a fixed underlying
// type, and inside a typedef.
enum { ANON = 10 };
enum Tagged { TAG = 5 };
enum Fixed : unsigned char { FIX = 200 };
typedef enum { TD_A, TD_B = 7 } Td;

static int pick(int a, int b, int c, int d) {
    return a + b + (c == 200 ? 20 : 0) + d - 10;
}

@comptime void gen(void) {
    Obj  *fn      = MakeFunction("made", GetType("int"));
    Node *args[4] = {MakeVarRef("ANON"), MakeVarRef("TAG"), MakeVarRef("FIX"),
                     MakeVarRef("TD_B")};
    FunctionSetBody(fn, MakeReturn(MakeFuncCall(MakeVarRef("pick"), args, 4)));
}
gen();

int main(void) {
    return made() + 10; // made() is 10 + 5 + 20 + 7 - 10 = 32
}
