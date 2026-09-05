// Fixture for tests/test_serialize_shared_header_unrelated_define_1305.c
// (#1305). A vendored-single-header-library-shaped body (like
// vendored_1301_lib.h/vendored_1304_lib.h) whose IMPLEMENTATION-gated
// function body uses a macro from a DIFFERENT, plain shared header
// (plain_config_1305.h) rather than one of its own -- the shape that
// exposed #1304's fix relocating the WRONG header's #include.
#ifndef VENDORED_1305_LIB_H
#define VENDORED_1305_LIB_H

#ifdef VENDORED_1305_IMPLEMENTATION

int v1305_use_cfg(int x) {
    return x + CFG_1305_VAL;
}

#endif // VENDORED_1305_IMPLEMENTATION

#endif // VENDORED_1305_LIB_H
