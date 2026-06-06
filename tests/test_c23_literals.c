// JCC_FLAGS: --std=c23
// Test C23 digit separators (single quotes in numeric literals)

int main() {
    // Test decimal literals with separators
    int a = 1'000'000;
    if (a != 1000000) return 1;

    int b = 1'234'567;
    if (b != 1234567) return 2;

    int c = 100'000;
    if (c != 100000) return 3;

    // Test with smaller numbers
    int d = 1'000;
    if (d != 1000) return 4;

    int e = 10'000;
    if (e != 10000) return 5;

    // Test hexadecimal with separators
    int h1 = 0x1'23'45;
    if (h1 != 0x12345) return 6;

    int h2 = 0xFF'FF;
    if (h2 != 0xFFFF) return 7;

    int h3 = 0xDE'AD'BE'EF;
    if (h3 != 0xDEADBEEF) return 8;

    // Test binary with separators
    int bin1 = 0b1010'1010;
    if (bin1 != 0b10101010) return 9;

    int bin2 = 0b1111'0000'1111'0000;
    if (bin2 != 0b1111000011110000) return 10;

    int bin3 = 0b1'0'1'0;
    if (bin3 != 0b1010) return 11;

    // Test octal with separators
    int oct1 = 01'23'45;
    if (oct1 != 012345) return 12;

    int oct2 = 07'77;
    if (oct2 != 0777) return 13;

    // Test with suffixes
    long l = 1'000'000L;
    if (l != 1000000L) return 14;

    unsigned int u = 1'000'000U;
    if (u != 1000000U) return 15;

    unsigned long ul = 1'000'000UL;
    if (ul != 1000000UL) return 16;

    // Test various separator positions
    int sep1 = 1'2'3'4;
    if (sep1 != 1234) return 17;

    int sep2 = 12'34;
    if (sep2 != 1234) return 18;

    int sep3 = 123'4;
    if (sep3 != 1234) return 19;

    // Test large numbers
    long big = 1'000'000'000;
    if (big != 1000000000) return 20;

    // Test empty initializers with digit separators
    int arr[] = {1'000, 2'000, 3'000};
    if (arr[0] != 1000) return 21;
    if (arr[1] != 2000) return 22;
    if (arr[2] != 3000) return 23;

    return 42;  // Success
}
