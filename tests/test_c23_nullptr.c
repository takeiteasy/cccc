// Test C23 nullptr keyword and nullptr_t type
#include <stddef.h>

int *global_p = nullptr;

int *take_ptr(int *p) {
    return p;
}

int main(void) {
    // Basic null pointer assignment via different pointer types
    int *ip = nullptr;
    char *cp = nullptr;
    void *vp = nullptr;
    if (ip != nullptr) return 1;
    if (cp != nullptr) return 2;
    if (vp != nullptr) return 3;

    // Comparisons in both orderings
    if (!(ip == nullptr)) return 4;
    if (!(nullptr == ip)) return 5;
    if (nullptr != ip) return 6;
    if (ip != nullptr) return 7;

    // Global initialized with nullptr
    if (global_p != nullptr) return 8;

    // Passing nullptr as a function argument
    if (take_ptr(nullptr) != nullptr) return 9;

    // Ternary with nullptr
    int x = 1;
    int *tp = x ? nullptr : ip;
    if (tp != nullptr) return 10;

    // sizeof(nullptr) and nullptr_t
    if (sizeof(nullptr) != sizeof(void *)) return 11;

    nullptr_t np = nullptr;
    if (sizeof(np) != sizeof(void *)) return 12;
    if (np != nullptr) return 13;

    // Reassign a pointer back to nullptr after pointing elsewhere
    int v = 5;
    ip = &v;
    if (ip == nullptr) return 14;
    ip = nullptr;
    if (ip != nullptr) return 15;

    return 42;
}
