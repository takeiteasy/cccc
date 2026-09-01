// CCCC_FLAGS: tests/fixtures/serialize_generated_secondary_1262.c -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: (?=[\s\S]*#include <wctype\.h>)(?=[\s\S]*int answer_1262\(void\))
// CCCC_REJECT_STDOUT: (?:#include <iso646\.h>|#define CCCC_1262_SECONDARY_LEAK)
//
// Ticket #1262: `cccc -c=generated` auto-captured every command-line
// input's own top-level preprocessor directives, so a `.c` module passed
// only so its function bodies could be forwarded into the comptime program
// leaked its `#include` / `#define` lines into the serialized output --
// which also forced an extra `-I` onto the downstream `cc` for a leaked
// quoted include. Now only the primary input's directives are replayed.
//
// This file is the non-primary input. Its `#include <iso646.h>` and
// `#define` below must be absent from the generated output; the primary
// fixture's own `#include <wctype.h>` and its generated `answer_1262`
// function must be present. An @emit/@shared route would still opt a
// directive in from here -- these are deliberately plain.
#include <iso646.h>
#define CCCC_1262_SECONDARY_LEAK 1

int secondary_helper_1262(void) {
    return CCCC_1262_SECONDARY_LEAK;
}
