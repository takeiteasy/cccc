// Fixture for tests/test_serialize_third_includer_unrelated_define_1307.c
// (#1307). A vendored-single-header-library-shaped body whose
// IMPLEMENTATION-gated function uses a macro from a DIFFERENT, plain
// shared header (plain_config_1307.h).
#ifndef VENDORED_1307_LIB_H
#define VENDORED_1307_LIB_H

#ifdef VENDORED_1307_IMPLEMENTATION

int v1307_use_cfg(int x) {
    return x + CFG_1307_VAL;
}

#endif // VENDORED_1307_IMPLEMENTATION

#endif // VENDORED_1307_LIB_H
