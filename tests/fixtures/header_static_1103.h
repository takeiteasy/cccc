#ifndef FIXTURE_HEADER_STATIC_1103_H
#define FIXTURE_HEADER_STATIC_1103_H

// #1103 regression fixture: `getline` is a genuine libc-exported symbol on
// both macOS (Darwin libSystem) and Linux (glibc) -- dlsym finds it on
// either host -- but nothing this test also includes declares it, so this
// `static inline` is the only in-scope declaration. Mirrors the shape that
// tripped #1103 for real: include/ndbm.h's five `static inline dbm_*`
// shims (dbm_store et al), each also a genuine libc symbol name. A
// synthetic name is used here instead of ndbm.h itself so the regression
// test doesn't depend on libgdbm-compat/CCCC_HAS_NDBM being available on
// every CI host (see test_serialize_header_static_norename_1103.c).
static inline int getline(void) {
    return 42;
}

#endif
