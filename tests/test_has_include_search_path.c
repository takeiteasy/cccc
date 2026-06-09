// CCCC_FLAGS: -I./tests/include_search
#if !__has_include(<has_probe.h>)
#error "expected -I header to be visible to __has_include"
#endif

#include <has_probe.h>

int main(void) {
    return HAS_PROBE_VALUE * 2;
}
