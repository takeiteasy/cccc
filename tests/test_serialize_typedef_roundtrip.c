// Regression fixture for -M typedef alias serialization

typedef int SerInt;
typedef SerInt *SerIntPtr;
typedef SerInt SerIntArray[3];
typedef int (*SerCallback)(int);

struct SerTagged {
    SerInt value;
};
typedef struct SerTagged SerTaggedAlias;

typedef struct {
    SerInt width;
    SerInt height;
} SerAnonStruct;

typedef enum {
    SER_LOCAL_RED = 1,
    SER_LOCAL_GREEN = 2,
} SerEnumAlias;

typedef union {
    SerInt i;
    char c;
} SerUnionAlias;

int main(void) {
    typedef struct {
        SerInt x;
        SerInt y;
    } LocalPoint;
    typedef SerInt LocalScalar;
    typedef LocalScalar *LocalPtr;
    typedef int (*LocalCallback)(int);

    SerInt n;
    SerTaggedAlias tagged;
    SerAnonStruct anon;
    SerEnumAlias color;
    SerUnionAlias uni;
    LocalPoint point;
    LocalScalar local;
    SerInt sum;

    n = 10;
    tagged.value = 5;
    anon.width = 6;
    anon.height = 7;
    color = SER_LOCAL_GREEN;
    uni.i = 8;
    point.x = 9;
    point.y = 10;
    local = 4;
    sum = 0;
    sum = sum + n;
    sum = sum + 1;
    sum = sum + 2;
    sum = sum + 3;
    sum = sum + tagged.value;
    sum = sum + anon.width;
    sum = sum + anon.height;
    sum = sum + color;
    sum = sum + uni.i;
    sum = sum + point.x;
    sum = sum + point.y;
    sum = sum + local;
    return sum - 25;
}
