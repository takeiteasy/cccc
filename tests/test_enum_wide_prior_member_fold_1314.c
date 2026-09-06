// #1314: an enum constant defined as an expression over *earlier
// enumerators of the same enum* whose values need more than 32 bits used
// to truncate each earlier reference to `int`.
//
// enum_specifier (src/parse_types.c) picks the enum's underlying type from
// the enumerator values only after the whole body is parsed (#1175/#1205).
// While the body is still open the enum Type keeps its default `int` size,
// so a reference to an earlier enumerator was typed `int`,
// usual_arith_conv() folded `A | B` in `int`, and eval2()'s ND_CAST arm
// truncated the wide operand to 0 -- `enum { A = 1ULL<<40, B = 1ULL<<2,
// ALL = A|B }` gave ALL == 4 (gcc-16/clang, C17 and C23, all give
// 1099511627780). Found via #1132's self-hosting spike: src/cccc.h's
// CCCC_WARN_ALL / CCCC_WARN_EXTRA are exactly this shape, so a
// self-hosted cccc lost every -Wall/-Wextra warning whose flag bit is
// >= 32.
//
// parse_postfix.c now widens the enumerator-reference node to a type that
// holds its value whenever the not-yet-finalized enum type is too narrow.

enum Wide {
    W_A   = 1ULL << 40,
    W_B   = 1ULL << 2,
    W_OR  = W_A | W_B,       // 1099511627780
    W_SUM = W_A + W_B,       // 1099511627780
    W_C   = W_A,             // plain copy of a wide earlier enumerator
    W_SH  = (W_A >> 38) | W_B, // (4) | (4) == 4
};

// A same-enum reference mixed with a wide literal was already correct
// (the literal forces a 64-bit common type) -- keep it working.
enum Mixed {
    M_A  = 1ULL << 33,
    M_LIT = M_A | (1ULL << 2),
};

// An all-fits-`int` enum must stay exactly as before (#1205): every
// enumerator identifier is `int`, the enum's own type is `unsigned int`.
enum Small { S_A = 1, S_B = 2, S_OR = S_A | S_B };

_Static_assert(W_OR == ((1ULL << 40) | (1ULL << 2)), "W_OR");
_Static_assert(W_SUM == ((1ULL << 40) + (1ULL << 2)), "W_SUM");
_Static_assert(W_C == (1ULL << 40), "W_C");
_Static_assert(W_SH == 4, "W_SH");
_Static_assert(M_LIT == ((1ULL << 33) | (1ULL << 2)), "M_LIT");
_Static_assert(S_OR == 3, "S_OR");
_Static_assert(sizeof(enum Wide) == 8, "enum Wide underlying type");
_Static_assert(sizeof(enum Small) == 4, "enum Small underlying type");

int main(void) {
    long long acc = 0;
    acc += (W_OR == 1099511627780LL);
    acc += (W_SUM == 1099511627780LL);
    acc += (W_C == (1LL << 40));
    acc += (W_SH == 4);
    acc += (M_LIT == ((1LL << 33) | 4));
    acc += (S_OR == 3);
    // 6 checks pass -> 6; scale to 42.
    return acc == 6 ? 42 : (int)acc;
}
