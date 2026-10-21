// Host-divergent scalar sizes round-trip under -c=native.

#include <signal.h>
#include <stdarg.h>

// #1169, follow-up to #1168: type_layout_is_host_owned() restricted itself
// to TY_STRUCT/TY_UNION/TY_ENUM to fix #1168's spurious-scalar-member false
// positive, but that also stopped a from_include *scalar* typedef whose
// real host layout genuinely differs (sigset_t: unsigned int/4 bytes in
// CCCC's own include/signal.h, 128 bytes on glibc) from ever
// re-materializing -- reintroducing the #1031 hazard for it. Fixed via
// find_typedef_name_exact()'s pointer-identity lookup: parse_typedef()
// already copy_type()s every non-aggregate typedef, so sigset_t has its own
// Type identity distinct from the bare `unsigned int` it aliases -- no
// structural (same_type_or_origin()) match needed, so #1168's bug can't
// reopen.

// Reached through an aggregate member too (this ticket's chosen scope).
struct Wrap1169 {
    sigset_t s;
};

int main(void) {
    unsigned long sigset_sz = sizeof(sigset_t);
    unsigned long wrap_sz   = sizeof(struct Wrap1169);
    unsigned long long_sz   = sizeof(long);
    unsigned long uint_sz   = sizeof(unsigned int);
    unsigned long va_sz     = sizeof(va_list);

    if (sigset_sz == 0)
        return 1;
    if (wrap_sz < sigset_sz)
        return 2;
    if (long_sz != sizeof(long))
        return 3;
    if (uint_sz != sizeof(unsigned int))
        return 4;
    if (va_sz != sizeof(va_list))
        return 5;

    return 42;
}
