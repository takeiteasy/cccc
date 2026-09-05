// Fixture for tests/test_serialize_third_includer_unrelated_define_1307.c
// (#1307). A plain, non-IMPLEMENTATION-style shared header -- like
// fixtures/plain_config_1305.h, but #include'd by THREE TUs instead of
// two, exercising push_emit_directive()'s #1305 fix at the point it was
// still imprecise: a #define captured anywhere earlier in a TU (not
// necessarily immediately ahead of its own #include) still wrongly won.
#ifndef PLAIN_CONFIG_1307_H
#define PLAIN_CONFIG_1307_H

#define CFG_1307_VAL 5

int cfg_1307_helper(int x);

#endif // PLAIN_CONFIG_1307_H
