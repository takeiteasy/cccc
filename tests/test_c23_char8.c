// Test C23 char8_t typedef and u8'x' character literals
#include <uchar.h>

int main(void) {
    // char8_t is typedef unsigned char
    if (sizeof(char8_t) != 1) return 1;

    // u8'A' has value 0x41 and fits in unsigned char
    char8_t c = u8'A';
    if (c != 0x41) return 2;
    if (sizeof(u8'A') != 1) return 3;

    // u8'\n' escape
    char8_t nl = u8'\n';
    if (nl != 10) return 4;

    // Arithmetic with char8_t
    char8_t arr[4];
    arr[u8'A' - 0x41] = 42;
    if (arr[0] != 42) return 5;

    // Assignment from u8 literal
    char8_t z = u8'\0';
    if (z != 0) return 6;

    // Store in unsigned char variable (should be compatible)
    unsigned char uc = u8'Z';
    if (uc != 0x5A) return 7;

    return 42;
}
