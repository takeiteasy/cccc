// Ticket #517: backtick quasi-quoting with ${...} interpolation.
// Note: primary-file #defines are not visible in comptime bodies (#627).
// IDENTITY is defined inside the comptime body where it is used.

typedef struct { int value; } Point;

[[cccc::comptime]]
Node *pick_second(Node *a, Node *b) {
    return b;
}

[[cccc::comptime]]
Node *plain_quote(void) {
    return `return 42;`;
}

[[cccc::comptime]]
Node *single_splice(Node *x) {
    // Macro defined in-body is visible within this comptime function's scope.
    #define IDENTITY(x) (x)
    return `return ${ IDENTITY(x) } + 1;`;
}

[[cccc::comptime]]
Node *multiple_splices(Node *a, Node *b) {
    return `return ${ a } + ${ b };`;
}

[[cccc::comptime]]
Node *call_splice(Node *a, Node *b) {
    return `return ${ pick_second(a, b) };`;
}

[[cccc::comptime]]
Node *nested_brace_splice(Node *a, Node *b) {
    return `return ${ ((Node *[]){ a, b })[1] };`;
}

[[cccc::comptime]]
Node *ignored_braces_splice(Node *x) {
    return `return ${ /* } */ (sizeof("}") && ('}' == '}')) ? x : x };`;
}

[[cccc::comptime]]
Node *reflect_in_quote(void) {
    return `return sizeof($Point);`;
}

[[cccc::comptime]]
Node *escaped_backtick(void) {
    return `return '\`';`;
}

[[cccc::comptime]]
Node *preserved_backslash(void) {
    return `return '\n';`;
}

[[cccc::comptime]]
Node *literal_placeholder_text(void) {
    return `return sizeof("$1");`;
}

[[cccc::comptime]]
Node *multiline_quote(void) {
    return `{
        int value = 40;
        return value + 2;
    }`;
}

int plain_result(void) { plain_quote(); }
int single_result(void) { single_splice(41); }
int multiple_result(void) { multiple_splices(20, 22); }
int call_result(void) { call_splice(1, 42); }
int nested_result(void) { nested_brace_splice(1, 42); }
int ignored_braces_result(void) { ignored_braces_splice(42); }
int reflect_result(void) { reflect_in_quote(); }
int backtick_result(void) { escaped_backtick(); }
int backslash_result(void) { preserved_backslash(); }
int placeholder_text_result(void) { literal_placeholder_text(); }
int multiline_result(void) { multiline_quote(); }

int main(void) {
    if (plain_result() != 42) return 1;
    if (single_result() != 42) return 2;
    if (multiple_result() != 42) return 3;
    if (call_result() != 42) return 4;
    if (nested_result() != 42) return 5;
    if (ignored_braces_result() != 42) return 6;
    if (reflect_result() != sizeof(void *)) return 7;
    if (backtick_result() != '`') return 8;
    if (backslash_result() != '\n') return 9;
    if (placeholder_text_result() != 3) return 10;
    if (multiline_result() != 42) return 11;
    return 42;
}
