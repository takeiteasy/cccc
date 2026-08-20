#include <string.h>
// Ticket #893: build_macro_context_tokens used to drop every file-scope
// declaration with a top-level '=' unconditionally, so an initialized
// runtime global was completely invisible to a [[cccc::comptime]] body --
// referencing one produced "undefined variable" even though the value is
// right there in the primary source file. A constant-initialized global
// declared directly in the primary file (string/int/array literal, no
// identifiers in the initializer) is now forwarded into the comptime
// declaration snapshot so its value is actually readable at compile time.

static const char *g_str    = "hello";
static const int   g_num    = 4;
static const int   g_arr[3] = {1, 2, 3};

[[cccc::comptime]]
Node *gen(void) {
    int len = (int)strlen(g_str) + g_num + g_arr[0] + g_arr[1] + g_arr[2];
    return MakeIntLiteral(len);
}

int result = gen();

int main(void) {
    // strlen("hello") = 5, + g_num (4) + g_arr{1,2,3} (6) = 15
    return result == 15 ? 42 : 1;
}
