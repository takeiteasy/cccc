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

void register_locale_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "setlocale", (void*)wrap_setlocale, 2, 0);
    cc_register_cfunc(vm, "localeconv", (void*)localeconv, 0, 0);
}
