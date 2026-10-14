// The attribute names find_attribute_macro treats as reflection.h handlers
// match the handlers reflection.h actually registers.

#include "internal.h"

#include <stdio.h>
#include <string.h>

static int failed = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL (%s:%d): ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

int main(void) {
    VirtualMachine vm;
    cc_init(&vm, 0);
    ensure_reflection_attrs_registered(&vm);

    int registered = 0;
    for (MacroFn *pm = vm.compiler.macro_fns; pm; pm = pm->next) {
        if (!pm->is_attribute_handler || !pm->attribute_name)
            continue;
        registered++;
        CHECK(is_reflection_builtin_attr(pm->attribute_name,
                                         (int)strlen(pm->attribute_name)),
              "reflection.h registers '%s' but it is not in the list",
              pm->attribute_name);
    }

    int listed = 0;
    for (const char *const *n = reflection_builtin_attr_names(); *n; n++) {
        listed++;
        bool found = false;
        for (MacroFn *pm = vm.compiler.macro_fns; pm; pm = pm->next)
            if (pm->is_attribute_handler && pm->attribute_name &&
                !strcmp(pm->attribute_name, *n))
                found = true;
        CHECK(found, "'%s' is listed but reflection.h does not register it",
              *n);
    }
    CHECK(registered == listed, "registered %d handlers, list has %d",
          registered, listed);

    cc_destroy(&vm);
    if (!failed)
        printf("PASS: reflection attribute list matches reflection.h\n");
    return failed;
}
