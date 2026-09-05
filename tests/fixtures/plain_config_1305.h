// Fixture for tests/test_serialize_shared_header_unrelated_define_1305.c
// (#1305). A plain, non-IMPLEMENTATION-style shared header -- no macro
// configures it, unlike fixtures/vendored_1304_lib.h/vendored_1301_lib.h.
// #include'd identically by two TUs so push_emit_directive()'s #1304
// identical-text dedup fires on it.
#ifndef PLAIN_CONFIG_1305_H
#define PLAIN_CONFIG_1305_H

#define CFG_1305_VAL 5

int cfg_1305_helper(int x);

#endif // PLAIN_CONFIG_1305_H
