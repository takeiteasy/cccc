#include "../src/internal.h"

int main(void) {
    if (!cc_llvm_backend_enabled())
        return 1;
    if (!cc_llvm_backend_version() || !cc_llvm_backend_version()[0])
        return 1;
    if (cc_llvm_backend_smoke_test() != 0)
        return 1;
    return 0;
}
