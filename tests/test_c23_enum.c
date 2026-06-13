// C23 enum enhancements: underlying type, forward declaration, wide values
#include <stdint.h>

// --- Underlying type: unsigned char (1 byte) ---
enum ByteColor : unsigned char { BC_RED = 0, BC_GREEN = 1, BC_BLUE = 255 };

// --- Underlying type: short (2 bytes) ---
enum ShortDir : short { SD_NORTH = -1, SD_SOUTH = 1 };

// --- Underlying type: unsigned long (8 bytes) ---
enum WideFlags : unsigned long { WF_NONE = 0, WF_A = 0x100000000UL, WF_B = 0x200000000UL };

// --- Underlying type: long long for large signed values ---
enum BigVal : long long {
    BV_MAX  =  0x3FFFFFFFFFFFFFFFLL,
    BV_NEG  = -0x3FFFFFFFFFFFFFFFLL
};

// --- Forward declaration with underlying type, then definition ---
enum Fwd : int;
enum Fwd { FWD_A = 10, FWD_B = 20 };

// --- Plain int-sized enum still works (no underlying type) ---
enum Plain { P_X = 7, P_Y = 42 };

// --- Typedef with underlying type ---
typedef enum TdEnum : unsigned char { TD_ZERO = 0, TD_MAX = 200 } TdEnum;

int main(void) {
    // size / align of underlying-typed enums
    if (sizeof(enum ByteColor) != 1) return 1;
    if (sizeof(enum ShortDir)  != 2) return 2;
    if (sizeof(enum WideFlags) != 8) return 3;
    if (sizeof(enum BigVal)    != 8) return 4;

    // Correct storage and round-trip of unsigned char enum
    enum ByteColor c = BC_BLUE;
    if (c != 255) return 5;
    if ((unsigned char)c != 255) return 6;

    // Short enum with negative value
    enum ShortDir d = SD_NORTH;
    if (d != -1) return 7;

    // 8-byte unsigned enum value above INT32_MAX
    enum WideFlags f = WF_A;
    if (f != 0x100000000UL) return 8;
    f = WF_B;
    if (f != 0x200000000UL) return 9;

    // Large signed long long enum value
    enum BigVal bv = BV_MAX;
    if (bv != 0x3FFFFFFFFFFFFFFFLL) return 10;
    bv = BV_NEG;
    if (bv != -0x3FFFFFFFFFFFFFFFLL) return 11;

    // Forward-declared then completed enum
    enum Fwd fwd = FWD_A;
    if (fwd != 10) return 12;
    fwd = FWD_B;
    if (fwd != 20) return 13;

    // Plain int-sized enum is unchanged
    enum Plain p = P_Y;
    if (p != 42) return 14;
    if (sizeof(enum Plain) != 4) return 15;

    // Typedef'd enum with underlying type
    TdEnum td = TD_MAX;
    if (td != 200) return 16;
    if (sizeof(TdEnum) != 1) return 17;

    // unsigned enum: unsigned char underlying type means values wrap at 256
    enum ByteColor wrap = (enum ByteColor)256;
    // 256 as unsigned char wraps to 0
    if (wrap != 0) return 18;

    // Arithmetic with wide enum
    unsigned long wide_sum = WF_A + WF_B;
    if (wide_sum != 0x300000000UL) return 19;

    return 42;
}
