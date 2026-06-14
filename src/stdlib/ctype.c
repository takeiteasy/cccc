// ctype.h stdlib function registration
#include "../cccc.h"
#include <ctype.h>

// Register all ctype.h functions
void register_ctype_functions(VirtualMachine *vm) {
    // Character classification functions
    cc_register_cfunc(vm, "isalnum", (void*)isalnum, 1, 0);
    cc_register_cfunc(vm, "isalpha", (void*)isalpha, 1, 0);
    cc_register_cfunc(vm, "isdigit", (void*)isdigit, 1, 0);
    cc_register_cfunc(vm, "islower", (void*)islower, 1, 0);
    cc_register_cfunc(vm, "isprint", (void*)isprint, 1, 0);
    cc_register_cfunc(vm, "ispunct", (void*)ispunct, 1, 0);
    cc_register_cfunc(vm, "isspace", (void*)isspace, 1, 0);
    cc_register_cfunc(vm, "isupper", (void*)isupper, 1, 0);
    cc_register_cfunc(vm, "isxdigit", (void*)isxdigit, 1, 0);

    // Character conversion functions
    cc_register_cfunc(vm, "tolower", (void*)tolower, 1, 0);
    cc_register_cfunc(vm, "toupper", (void*)toupper, 1, 0);
}
