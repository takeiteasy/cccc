// __has_c_attribute reports a name only in the namespaces it has.

#if !__has_c_attribute(nodiscard) || !__has_c_attribute(gnu::nodiscard) ||     \
    __has_c_attribute(cccc::nodiscard)
#error standard
#endif
#if !__has_c_attribute(gnu::packed) || !__has_c_attribute(__gnu__::packed) ||  \
    __has_c_attribute(packed) || __has_c_attribute(cccc::packed) ||            \
    __has_c_attribute(clang::packed)
#error gnu
#endif
#if !__has_c_attribute(cccc::kernel) || !__has_c_attribute(kernel) ||          \
    __has_c_attribute(gnu::kernel)
#error cccc
#endif
#if !__has_c_attribute(cccc::global) || __has_c_attribute(global)
#error address space
#endif
#if !__has_c_attribute(clang::overloadable) ||                                 \
    !__has_c_attribute(__clang__::overloadable) ||                             \
    __has_c_attribute(gnu::overloadable) || __has_c_attribute(overloadable)
#error clang
#endif

int main(void) {
    return 42;
}
