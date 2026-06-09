// CCCC libFuzzer harness
// Compiles C source from memory to catch frontend crashes.
// Build: clang -g -O1 -fsanitize=fuzzer,address -o fuzz_harness \
//           src/fuzzing.c src/*.c src/stdlib/*.c -I./include
// Run:   ./fuzz_harness corpus/

#include "cccc.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

// Compile C source without requiring main() or executing.
// Returns 0 on success, 1 on compile error (expected for malformed input).
static int compile_only_from_string(const char *src, size_t len) {
    CCCC vm;
    cc_init(&vm, 0);
    vm.compiler.compile_only = true;
    vm.collect_errors = true;
    vm.max_errors = 5;

    // Include path for standard headers
    cc_include(&vm, "./include");
    cc_load_stdlib(&vm);

    // Set up error recovery via setjmp/longjmp
    jmp_buf err_buf;
    vm.error_jmp_buf = &err_buf;
    if (setjmp(err_buf) != 0) {
        // Compilation error — expected for fuzzed malformed input
        cc_destroy(&vm);
        return 1;
    }

    // Tokenize the input string
    char *buf = malloc(len + 1);
    if (!buf) {
        cc_destroy(&vm);
        return 1;
    }
    memcpy(buf, src, len);
    buf[len] = '\0';

    Token *tok = tokenize_string(&vm, "<fuzz>", buf);
    if (!tok) {
        free(buf);
        cc_destroy(&vm);
        return 1;
    }

    // Preprocess
    tok = preprocess(&vm, tok);
    if (!tok) {
        free(buf);
        cc_destroy(&vm);
        return 1;
    }

    // Check for errors after preprocessing
    if (cc_has_errors(&vm)) {
        free(buf);
        cc_destroy(&vm);
        return 1;
    }

    // Execute inline macros
    Token *tokens[1] = { tok };
    cc_execute_inline_macros(&vm, tokens, 1);

    // Parse
    Obj *prog = cc_parse(&vm, tok);
    if (!prog) {
        free(buf);
        cc_destroy(&vm);
        return 1;
    }

    // Check for errors after parsing
    if (cc_has_errors(&vm)) {
        free(buf);
        cc_destroy(&vm);
        return 1;
    }

    // Link
    Obj *merged = cc_link_progs(&vm, &prog, 1);
    if (!merged) {
        free(buf);
        cc_destroy(&vm);
        return 1;
    }

    // Expand macros
    cc_expand_macros(&vm, merged);

    // Compile to bytecode (no main() required in compile_only mode)
    cc_compile(&vm, merged);

    free(buf);
    cc_destroy(&vm);
    return 0;
}

// libFuzzer entry point
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Ignore empty inputs
    if (size == 0)
        return 0;

    // Ensure null-termination safety by copying
    compile_only_from_string((const char *)data, size);
    return 0;
}
