// locale.h stdlib function registration
#include "../cccc.h"
#include <locale.h>

// setlocale() (fix for #819) -- CCCC's guest-visible LC_* numbering
// (include/locale.h) doesn't match either host's real numbering (macOS
// happens to use 0-5 in ALL/COLLATE/CTYPE/MONETARY/NUMERIC/TIME order;
// glibc uses a completely different 0-6 ordering with LC_ALL last), so a
// direct passthrough silently addressed the wrong category on Linux. This
// translates the guest's canonical category to the host's real value
// before calling the real setlocale(), the same pattern _SC_*/_PC_*/_CS_*
// use in src/stdlib/posix.c.
static int guest_to_host_lc(int guest_category) {
    switch (guest_category) {
    case 0: return LC_ALL;
    case 1: return LC_COLLATE;
    case 2: return LC_CTYPE;
    case 3: return LC_MONETARY;
    case 4: return LC_NUMERIC;
    case 5: return LC_TIME;
    case 6: return LC_MESSAGES;
    default: return guest_category;
    }
}

static char *wrap_setlocale(int guest_category, const char *locale) {
    return setlocale(guest_to_host_lc(guest_category), locale);
}

// newlocale()/uselocale() family (#820) -- LC_*_MASK is CCCC's own
// canonical bitmask numbering, matching macOS's own assignment (see the
// file-top comment in include/locale.h), so translation is a no-op there.
// On Linux this maps each canonical bit to glibc's real LC_*_MASK value.
//
// LC_ALL_MASK is special-cased rather than mapped bit-by-bit: glibc's real
// LC_ALL_MASK ORs 12 categories (adds PAPER/NAME/ADDRESS/TELEPHONE/
// MEASUREMENT/IDENTIFICATION on top of the 6 CCCC exposes), so a canonical
// LC_ALL_MASK (0x3f) must translate straight to the host's LC_ALL_MASK, not
// to the OR of the 6 host masks CCCC knows about -- otherwise
// newlocale(LC_ALL_MASK, ...) would build a locale with those extra
// categories left unset.
static int guest_to_host_lc_mask(int guest_mask) {
    if (guest_mask == 0x3f) return LC_ALL_MASK;

    int host_mask = 0;
    if (guest_mask & (1 << 0)) host_mask |= LC_COLLATE_MASK;
    if (guest_mask & (1 << 1)) host_mask |= LC_CTYPE_MASK;
    if (guest_mask & (1 << 2)) host_mask |= LC_MESSAGES_MASK;
    if (guest_mask & (1 << 3)) host_mask |= LC_MONETARY_MASK;
    if (guest_mask & (1 << 4)) host_mask |= LC_NUMERIC_MASK;
    if (guest_mask & (1 << 5)) host_mask |= LC_TIME_MASK;
    return host_mask;
}

static locale_t wrap_newlocale(int guest_mask, const char *locale, locale_t base) {
    return newlocale(guest_to_host_lc_mask(guest_mask), locale, base);
}

// freelocale() returns int on macOS but void on glibc (POSIX). Normalize to
// a void guest-visible signature so the guest-side declaration doesn't have
// to pick a host-specific return type.
static void wrap_freelocale(locale_t locobj) {
    if (!locobj || locobj == LC_GLOBAL_LOCALE) return;
#ifdef __APPLE__
    (void)freelocale(locobj);
#else
    freelocale(locobj);
#endif
}

void register_locale_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "setlocale", (void*)wrap_setlocale, 2, 0);
    cc_register_cfunc(vm, "localeconv", (void*)localeconv, 0, 0);
    cc_register_cfunc(vm, "newlocale", (void*)wrap_newlocale, 3, 0);
    cc_register_cfunc(vm, "duplocale", (void*)duplocale, 1, 0);
    cc_register_cfunc(vm, "freelocale", (void*)wrap_freelocale, 1, 0);
    cc_register_cfunc(vm, "uselocale", (void*)uselocale, 1, 0);
}
