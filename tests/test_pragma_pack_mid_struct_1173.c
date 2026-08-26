// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: #pragma pack changed inside a struct

// A #pragma pack directive between two members of one struct is not
// supported (GCC applies it per-member mid-declaration; this implementation
// applies one value to the whole aggregate) -- reject it explicitly rather
// than silently applying the wrong value to some members (#1173).

struct MidStructPack1173 {
    char c;
#pragma pack(1)
    int a;
};

int main(void) {
    return 0;
}
