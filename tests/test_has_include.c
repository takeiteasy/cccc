int main(void) {
    int result = 0;

#if __has_include(<stdatomic.h>)
    result += 10;
#else
    return 1;
#endif

#if __has_include(<missing_jcc_header_280.h>)
    return 2;
#else
    result += 10;
#endif

#if __has_include("include_search/has_probe.h")
#include "include_search/has_probe.h"
    result += HAS_PROBE_VALUE;
#else
    return 3;
#endif

#if __has_include("missing_local_header_280.h")
#include "missing_local_header_280.h"
    return 4;
#else
    result += 1;
#endif

    return result;
}
