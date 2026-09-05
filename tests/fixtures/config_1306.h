// Fixture for tests/test_serialize_spelling_dedup_unrelated_define_1306.c
// (#1306). A plain, non-IMPLEMENTATION-style shared header -- like
// fixtures/plain_config_1305.h, but #include'd with TWO DIFFERENT
// spellings by its two TUs, routing through cc_serialize_program()'s own
// #1292 canonical-path dedup (serialize_program.c) rather than
// push_emit_directive()'s identical-text dedup (#1305's own route).
#ifndef CONFIG_1306_H
#define CONFIG_1306_H

#define CFG_1306_VAL 5

int cfg_1306_helper(int x);

#endif // CONFIG_1306_H
