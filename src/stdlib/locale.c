// locale.h stdlib function registration
#include "../jcc.h"
#include <locale.h>

void register_locale_functions(JCC *vm) {
    cc_register_cfunc(vm, "setlocale", (void*)setlocale, 2, 0);
    cc_register_cfunc(vm, "localeconv", (void*)localeconv, 0, 0);
}
