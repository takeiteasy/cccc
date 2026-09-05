// Fixture for tests/test_serialize_spelling_dedup_unrelated_define_1306.c
// (#1306). A vendored-single-header-library-shaped body whose
// IMPLEMENTATION-gated function uses a macro from a DIFFERENT, plain
// shared header (config_1306.h) reached under a different spelling by a
// second TU.
#ifndef VENDORED_1306_LIB_H
#define VENDORED_1306_LIB_H

#ifdef VENDORED_1306_IMPLEMENTATION

int v1306_use_cfg(int x) {
    return x + CFG_1306_VAL;
}

#endif // VENDORED_1306_IMPLEMENTATION

#endif // VENDORED_1306_LIB_H
