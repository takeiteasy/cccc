// Check va_list size
#include "stdarg.h"

int main() {
    // va_list is a struct with 3 members: char*, char*, int
    // On 64-bit VM: 8 + 8 + 4 = 20 bytes, but aligned to 24 bytes? Or might be
    // 8+8+8=24?
    return sizeof(va_list); // What size do we get?
}
