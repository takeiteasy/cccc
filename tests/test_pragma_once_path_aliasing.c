// CCCC_FLAGS: -I./tests/fixtures
//
// #1277: CCCC's `#pragma once` (and include-guard) suppression was keyed on
// the raw resolved path *string*, so one physical header reached under two
// spellings in a single TU was included twice and every `static inline`
// helper in it was redefined ("redefinition of ..."). A real host cc does
// not hit this because its `#pragma once` is inode/realpath-based. This is
// the exact shape src/macros.c trips: it includes both "./internal.h" and
// "./parse_internal.h", and the latter's own `#include "./internal.h"`
// resolves against its own directory to a different string
// ("src/././internal.h" vs "src/./internal.h").
//
// Fixed by canonicalizing the map key with realpath() (pragma_once_key,
// src/preprocess.c) on every get and put, falling back to the literal
// string for synthetic/embedded paths.
#include "pragma_once_alias_1277.h"
#include "pragma_once_alias_1277_via.h"

int main(void) {
    return pragma_once_alias_answer();
}
