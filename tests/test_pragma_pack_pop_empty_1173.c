// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: pack\(pop\) with no matching push

// #pragma pack(pop) with no preceding #pragma pack(push, ...) is a hard
// error, matching GCC/MSVC (#1173).

#pragma pack(pop)

int main(void) {
    return 0;
}
