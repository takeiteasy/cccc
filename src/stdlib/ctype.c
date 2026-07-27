// ctype.h stdlib function registration
#include "../cccc.h"
#include <ctype.h>
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif

// Register all ctype.h functions
void register_ctype_functions(VirtualMachine *vm) {
    // Character classification functions
    cc_register_cfunc(vm, "isalnum",  (void*)isalnum,  1, 0);
    cc_register_cfunc(vm, "isalpha",  (void*)isalpha,  1, 0);
    cc_register_cfunc(vm, "isblank",  (void*)isblank,  1, 0);
    cc_register_cfunc(vm, "iscntrl",  (void*)iscntrl,  1, 0);
    cc_register_cfunc(vm, "isdigit",  (void*)isdigit,  1, 0);
    cc_register_cfunc(vm, "isgraph",  (void*)isgraph,  1, 0);
    cc_register_cfunc(vm, "islower",  (void*)islower,  1, 0);
    cc_register_cfunc(vm, "isprint",  (void*)isprint,  1, 0);
    cc_register_cfunc(vm, "ispunct",  (void*)ispunct,  1, 0);
    cc_register_cfunc(vm, "isspace",  (void*)isspace,  1, 0);
    cc_register_cfunc(vm, "isupper",  (void*)isupper,  1, 0);
    cc_register_cfunc(vm, "isxdigit", (void*)isxdigit, 1, 0);

    // Character conversion functions
    cc_register_cfunc(vm, "tolower", (void*)tolower, 1, 0);
    cc_register_cfunc(vm, "toupper", (void*)toupper, 1, 0);

    // _l family (#820) -- takes an explicit locale_t instead of consulting
    // the process-global/per-thread locale. On macOS these are
    // xlocale/_ctype.h static inline functions rather than real extern
    // symbols; taking their address is still legal C from within this
    // translation unit (the compiler emits a private instance on demand).
    cc_register_cfunc(vm, "isalnum_l",  (void*)isalnum_l,  2, 0);
    cc_register_cfunc(vm, "isalpha_l",  (void*)isalpha_l,  2, 0);
    cc_register_cfunc(vm, "isblank_l",  (void*)isblank_l,  2, 0);
    cc_register_cfunc(vm, "iscntrl_l",  (void*)iscntrl_l,  2, 0);
    cc_register_cfunc(vm, "isdigit_l",  (void*)isdigit_l,  2, 0);
    cc_register_cfunc(vm, "isgraph_l",  (void*)isgraph_l,  2, 0);
    cc_register_cfunc(vm, "islower_l",  (void*)islower_l,  2, 0);
    cc_register_cfunc(vm, "isprint_l",  (void*)isprint_l,  2, 0);
    cc_register_cfunc(vm, "ispunct_l",  (void*)ispunct_l,  2, 0);
    cc_register_cfunc(vm, "isspace_l",  (void*)isspace_l,  2, 0);
    cc_register_cfunc(vm, "isupper_l",  (void*)isupper_l,  2, 0);
    cc_register_cfunc(vm, "isxdigit_l", (void*)isxdigit_l, 2, 0);
    cc_register_cfunc(vm, "tolower_l",  (void*)tolower_l,  2, 0);
    cc_register_cfunc(vm, "toupper_l",  (void*)toupper_l,  2, 0);
}
