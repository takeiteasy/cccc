// Serialize/Deserialize use only byte copies, so the buffer needs no
// alignment: this runs them on a buffer at an odd address.
#include <string.h>

struct Inner {
    short a;
    long  b;
};

struct Mixed {
    int          x;
    unsigned     lo : 3;
    unsigned     hi : 12;
    double       d;
    struct Inner in;
    struct {
        char  c;
        short s;
    };
    char tail;
};

[[cccc::comptime]]
void generate_mixed_serdes(void) {
    Type *ty   = GetType("Mixed");

    Obj  *pack = MakeFunction("mixed_pack", GetType("int"));
    FunctionAddParam(pack, "self", MakePointer(ty));
    FunctionAddParam(pack, "buf", MakePointer(GetType("void")));
    WithFn(pack) {
        Node *self  = MakeUnary(NK_DEREF, MakeParamRef(pack, "self"));
        Node *block = Serialize(ty, self, MakeParamRef(pack, "buf"));
        BlockAddStmt(block, MakeReturn(MakeIntLiteral(TypeSize(ty))));
        FunctionSetBody(pack, block);
    }

    Obj *unpack = MakeFunction("mixed_unpack", ty);
    FunctionAddParam(unpack, "buf", MakePointer(GetType("void")));
    WithFn(unpack) {
        FunctionSetBody(
            unpack, MakeReturn(Deserialize(ty, MakeParamRef(unpack, "buf"))));
    }

    Obj *unpack_int = MakeFunction("int_unpack", GetType("int"));
    FunctionAddParam(unpack_int, "buf", MakePointer(GetType("void")));
    WithFn(unpack_int) {
        FunctionSetBody(unpack_int,
                        MakeReturn(Deserialize(
                            GetType("int"), MakeParamRef(unpack_int, "buf"))));
    }
}

generate_mixed_serdes();

int main(void) {
    struct Mixed m;
    memset(&m, 0, sizeof m);
    m.x    = -7;
    m.lo   = 5;
    m.hi   = 3001;
    m.d    = 2.5;
    m.in.a = -3;
    m.in.b = 1234567890123L;
    m.c    = 'q';
    m.s    = 4242;
    m.tail = 'z';

    _Alignas(16) char storage[sizeof(struct Mixed) + 16];
    memset(storage, 0, sizeof storage);
    char *buf = storage + 1;

    if (mixed_pack(&m, buf) != (int)sizeof m)
        return 1;
    struct Mixed r = mixed_unpack(buf);
    if (memcmp(&m, &r, sizeof m) != 0)
        return 2;
    if (r.lo != 5 || r.hi != 3001 || r.in.b != 1234567890123L || r.s != 4242)
        return 3;

    int n = 0x01020304;
    memcpy(storage + 3, &n, sizeof n);
    if (int_unpack(storage + 3) != n)
        return 4;
    return 42;
}
