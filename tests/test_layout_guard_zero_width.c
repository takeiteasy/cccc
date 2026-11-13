// CCCC_NATIVE_GCC_MACOS_SKIP: gcc on macOS lays out unnamed bit-fields per AAPCS64, unlike Apple
// Expected return: 42
//
// Whether a width-0 unnamed bit-field contributes its type's alignment to the
// struct follows the real AAPCS64 rule (see CCCC_ALIGN_ANON_BITFIELDS in
// src/parse_types.c). A host compiler that disagrees fails the -c=native layout
// guard, correctly: the emitted C would be miscompiled.

struct tc_layoutguard_zero_width {
    char c;
    int : 0;
    char d;
};

struct tc_layoutguard_zero_width g_layoutguard_zero_width;

int main(void) {
#if defined(__aarch64__) && !defined(__APPLE__)
    _Static_assert(sizeof(struct tc_layoutguard_zero_width) == 8, "cccc");
    _Static_assert(_Alignof(struct tc_layoutguard_zero_width) == 4, "cccc");
#else
    _Static_assert(sizeof(struct tc_layoutguard_zero_width) == 5, "cccc");
    _Static_assert(_Alignof(struct tc_layoutguard_zero_width) == 1, "cccc");
#endif

    g_layoutguard_zero_width.c = 'x';
    g_layoutguard_zero_width.d = 'y';
    if (g_layoutguard_zero_width.c != 'x' || g_layoutguard_zero_width.d != 'y')
        return 1;
    return 42;
}
