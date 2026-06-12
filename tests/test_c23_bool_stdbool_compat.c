// Test that <stdbool.h> still works under C23, now that bool/true/false
// are keywords (stdbool.h should not redefine them in C23 mode).
#include <stdbool.h>
#include <stddef.h>

int main(void) {
    bool a = true;
    bool b = false;

    if (a != true) return 1;
    if (b != false) return 2;
    if (!__bool_true_false_are_defined) return 3;

    // nullptr_t available alongside stdbool.h
    nullptr_t np = nullptr;
    if (np != nullptr) return 4;

    return 42;
}
