/*
 JCC: JIT C Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.

 This file was original part of chibicc by Rui Ueyama (MIT) https://github.com/rui314/chibicc
*/

#include "jcc.h"
#include "./internal.h"

// Reports an error and exit (or longjmp if error handling is enabled).
void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

typedef struct {
    const char *name;
    uint64_t mask;
    bool is_group;
} WarningInfo;

static const WarningInfo warning_infos[] = {
    {"unused", JCC_WARN_UNUSED, false},
    {"implicit-function-declaration", JCC_WARN_IMPLICIT_FUNCTION_DECLARATION, false},
    {"implicit-int", JCC_WARN_IMPLICIT_INT, false},
    {"return-type", JCC_WARN_RETURN_TYPE, false},
    {"shadow", JCC_WARN_SHADOW, false},
    {"format", JCC_WARN_FORMAT, false},
    // -Wconversion is the umbrella: enables integer narrowing, sign-conversion,
    // and float-conversion.  The first entry maps the name to the full group
    // mask so -Wconversion / -Wno-conversion / -Werror=conversion all cover all
    // three sub-categories.  The second (non-group) entry exists only so that
    // jcc_warning_name(JCC_WARN_CONVERSION) (exact-bit match) still returns
    // "conversion" for the [-Wconversion] tag in integer-narrowing diagnostics.
    {"conversion", JCC_WARN_CONVERSION_GROUP, false},
    {"conversion", JCC_WARN_CONVERSION, false},
    {"sign-conversion", JCC_WARN_SIGN_CONVERSION, false},
    {"float-conversion", JCC_WARN_FLOAT_CONVERSION, false},
    {"sign-compare", JCC_WARN_SIGN_COMPARE, false},
    {"pointer-arith", JCC_WARN_POINTER_ARITH, false},
    {"pedantic", JCC_WARN_PEDANTIC, false},
    {"deprecated", JCC_WARN_DEPRECATED, false},
    {"cpp", JCC_WARN_CPP, false},
    {"extra-tokens", JCC_WARN_EXTRA_TOKENS, false},
    {"large-file-embed", JCC_WARN_LARGE_FILE_EMBED, false},
    {"jcc-macro", JCC_WARN_JCC_MACRO, false},
    {"all", JCC_WARN_ALL, true},
    {"extra", JCC_WARN_EXTRA, true},
};

const char *jcc_warning_name(JCCWarning warning) {
    uint64_t mask = (uint64_t)warning;
    for (size_t i = 0; i < sizeof(warning_infos) / sizeof(*warning_infos); i++)
        if (!warning_infos[i].is_group && warning_infos[i].mask == mask)
            return warning_infos[i].name;
    return NULL;
}

uint64_t jcc_warning_mask_for_name(const char *name) {
    for (size_t i = 0; i < sizeof(warning_infos) / sizeof(*warning_infos); i++)
        if (strcmp(warning_infos[i].name, name) == 0)
            return warning_infos[i].mask;
    return 0;
}

bool jcc_warning_is_group_name(const char *name) {
    for (size_t i = 0; i < sizeof(warning_infos) / sizeof(*warning_infos); i++)
        if (strcmp(warning_infos[i].name, name) == 0)
            return warning_infos[i].is_group;
    return false;
}

// Reports a diagnostic message in the following format.
//
// foo.c:10: x = y + 1;
//               ^ error: <message here>
static void vdiagnostic_at(JCC *vm, char *filename, char *input, int line_no,
                           char *loc, const char *kind,
                           const char *warn_name, char *fmt, va_list ap) {
    // Find a line containing `loc`.
    char *line = loc;
    while (input < line && line[-1] != '\n')
        line--;

    char *end = loc;
    while (*end && *end != '\n')
        end++;

    // If error handling or error collection is enabled, save error to buffer
    if (vm && (vm->error_jmp_buf || vm->collect_errors)) {
        // Build error message into a buffer
        char *msg = arena_alloc(&vm->compiler.parser_arena, 4096);  // Allocate space for error message
        if (!msg) {
            fprintf(stderr, "Failed to allocate error message buffer\n");
            exit(1);
        }
        memset(msg, 0, 4096);

        // Format the diagnostic message
        int pos = snprintf(msg, 4096, "%s:%d: ", filename, line_no);
        if (pos > 4096) pos = 4096;
        pos += snprintf(msg + pos, 4096 - pos, "%.*s\n", (int)(end - line), line);
        if (pos > 4096) pos = 4096;

        int indent = strlen(filename) + snprintf(NULL, 0, ":%d: ", line_no);
        int col_offset = display_width(vm, line, loc - line) + indent;
        pos += snprintf(msg + pos, 4096 - pos, "%*s^ %s: ", col_offset, "", kind);

        va_list ap_copy;
        va_copy(ap_copy, ap);
        int written = vsnprintf(msg + pos, 4096 - pos, fmt, ap_copy);
        va_end(ap_copy);
        if (written > 0) {
            pos += written;
            if (pos > 4096) pos = 4096;
        }
        if (warn_name && pos < 4096) {
            int suffix = snprintf(msg + pos, 4096 - pos, " [-W%s]", warn_name);
            if (suffix > 0) {
                pos += suffix;
                if (pos > 4096) pos = 4096;
            }
        }
        if (pos < 4096)
            snprintf(msg + pos, 4096 - pos, "\n");

        vm->error_message = msg;
        return;  // Don't print to stderr or exit
    }

    // Normal mode: print to stderr
    int indent = fprintf(stderr, "%s:%d: ", filename, line_no);
    fprintf(stderr, "%.*s\n", (int)(end - line), line);

    // Show the error message.
    int pos = display_width(vm, line, loc - line) + indent;

    fprintf(stderr, "%*s", pos, ""); // print pos spaces.
    fprintf(stderr, "^ %s: ", kind);
    vfprintf(stderr, fmt, ap);
    if (warn_name)
        fprintf(stderr, " [-W%s]", warn_name);
    fprintf(stderr, "\n");
}

static void verror_at(JCC *vm, char *filename, char *input, int line_no,
                      char *loc, char *fmt, va_list ap) {
    vdiagnostic_at(vm, filename, input, line_no, loc, "error", NULL, fmt, ap);
}

void error_at(JCC *vm, char *loc, char *fmt, ...) {
    int line_no = 1;
    for (char *p = vm->compiler.current_file->contents; p < loc; p++)
        if (*p == '\n')
            line_no++;

    // Calculate column number
    int col_no = 1;
    char *line_start = loc;
    while (line_start > vm->compiler.current_file->contents && line_start[-1] != '\n') {
        line_start--;
        col_no++;
    }

    va_list ap;
    va_start(ap, fmt);
    verror_at(vm, vm->compiler.current_file->name, vm->compiler.current_file->contents, line_no, loc, fmt, ap);
    va_end(ap);

    // Collect error if error collection is enabled
    if (vm && vm->collect_errors && vm->error_message) {
        CompileError *err = arena_alloc(&vm->compiler.parser_arena, sizeof(CompileError));
        err->message = vm->error_message;
        err->filename = vm->compiler.current_file->name;
        err->line_no = line_no;
        err->col_no = col_no;
        err->severity = 0; // error
        err->warn_name = NULL;
        err->next = NULL;

        // Append to list
        if (!vm->errors) {
            vm->errors = vm->errors_tail = err;
        } else {
            vm->errors_tail->next = err;
            vm->errors_tail = err;
        }
        vm->error_count++;
        vm->error_message = NULL; // Clear so it's not reused
    }

    // Always use longjmp/exit (Level 1: no parser recovery)
    if (vm && vm->error_jmp_buf) {
        longjmp(*vm->error_jmp_buf, 1);
    }
    exit(1);
}

void error_tok(JCC *vm, Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(vm, tok->file->name, tok->file->contents, tok->line_no, tok->loc, fmt, ap);
    va_end(ap);

    // Collect error if error collection is enabled
    if (vm && vm->collect_errors && vm->error_message) {
        CompileError *err = arena_alloc(&vm->compiler.parser_arena, sizeof(CompileError));
        err->message = vm->error_message;
        err->filename = tok->file->name;
        err->line_no = tok->line_no;
        err->col_no = tok->col_no;
        err->severity = 0; // error
        err->warn_name = NULL;
        err->next = NULL;

        // Append to list
        if (!vm->errors) {
            vm->errors = vm->errors_tail = err;
        } else {
            vm->errors_tail->next = err;
            vm->errors_tail = err;
        }
        vm->error_count++;
        vm->error_message = NULL; // Clear so it's not reused
    }

    // Always use longjmp/exit (Level 1: no parser recovery)
    if (vm && vm->error_jmp_buf) {
        longjmp(*vm->error_jmp_buf, 1);
    }
    exit(1);
}

// Error reporting with recovery support (Level 2)
// Returns true if parsing should continue with recovery, false if max errors hit
bool error_tok_recover(JCC *vm, Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(vm, tok->file->name, tok->file->contents, tok->line_no, tok->loc, fmt, ap);
    va_end(ap);

    // Collect error if error collection is enabled
    if (vm && vm->collect_errors && vm->error_message) {
        CompileError *err = arena_alloc(&vm->compiler.parser_arena, sizeof(CompileError));
        err->message = vm->error_message;
        err->filename = tok->file->name;
        err->line_no = tok->line_no;
        err->col_no = tok->col_no;
        err->severity = 0; // error
        err->warn_name = NULL;
        err->next = NULL;

        // Append to list
        if (!vm->errors) {
            vm->errors = vm->errors_tail = err;
        } else {
            vm->errors_tail->next = err;
            vm->errors_tail = err;
        }
        vm->error_count++;
        vm->error_message = NULL; // Clear so it's not reused

        // Check if we've hit max errors
        if (vm->max_errors > 0 && vm->error_count >= vm->max_errors) {
            // Too many errors, bail out
            if (vm->error_jmp_buf) {
                longjmp(*vm->error_jmp_buf, 1);
            }
            return false;
        }

        return true;  // Continue with recovery
    }

    // If error collection not enabled, use old behavior
    if (vm && vm->error_jmp_buf) {
        longjmp(*vm->error_jmp_buf, 1);
    }
    exit(1);
}

void warn_tok(JCC *vm, Token *tok, JCCWarning category, char *fmt, ...) {
    uint64_t mask = (uint64_t)category;
    if (!vm || !(vm->compiler.warnings & mask))
        return;

    const char *warn_name = jcc_warning_name(category);
    bool is_error = (vm->warnings_as_errors &&
                     !(vm->compiler.warning_no_errors & mask)) ||
                    (vm->compiler.warning_errors & mask);

    va_list ap;
    va_start(ap, fmt);
    vdiagnostic_at(vm, tok->file->name, tok->file->contents, tok->line_no,
                   tok->loc, is_error ? "error" : "warning", warn_name, fmt, ap);
    va_end(ap);

    // If error_jmp_buf is set but collect_errors is false, print the warning now
    // (verror_at will have stored it in error_message without printing)
    if (vm->error_jmp_buf && !vm->collect_errors && !is_error && vm->error_message) {
        fprintf(stderr, "%s", vm->error_message);
        vm->error_message = NULL;
    }

    // If this warning is treated as an error, collect it as an error and abort.
    if (is_error) {
        if (vm->error_message) {
            CompileError *err = arena_alloc(&vm->compiler.parser_arena, sizeof(CompileError));
            err->message = vm->error_message;
            err->filename = tok->file->name;
            err->line_no = tok->line_no;
            err->col_no = tok->col_no;
            err->severity = 0; // error (not warning)
            err->warn_name = warn_name;
            err->next = NULL;

            // Append to list
            if (!vm->errors) {
                vm->errors = vm->errors_tail = err;
            } else {
                vm->errors_tail->next = err;
                vm->errors_tail = err;
            }
            vm->error_count++;  // Count as error
            vm->error_message = NULL;
        }

        // Use longjmp like error_tok
        if (vm->error_jmp_buf) {
            longjmp(*vm->error_jmp_buf, 1);
        }
        exit(1);
    }

    // Collect warning if error collection is enabled
    if (vm && vm->collect_errors && vm->error_message) {
        CompileError *err = arena_alloc(&vm->compiler.parser_arena, sizeof(CompileError));
        err->message = vm->error_message;
        err->filename = tok->file->name;
        err->line_no = tok->line_no;
        err->col_no = tok->col_no;
        err->severity = 1; // warning
        err->warn_name = warn_name;
        err->next = NULL;

        // Append to list
        if (!vm->errors) {
            vm->errors = vm->errors_tail = err;
        } else {
            vm->errors_tail->next = err;
            vm->errors_tail = err;
        }
        vm->warning_count++;
        vm->error_message = NULL; // Clear so it's not reused
    }
}

// Consumes the current token if it matches `op`.
bool equal(Token *tok, char *op) {
    return tok && strlen(op) == tok->len && memcmp(tok->loc, op, tok->len) == 0;
}

// Ensure that the current token is `op`.
Token *skip(JCC *vm, Token *tok, char *op) {
    if (!tok)
        error("expected '%s'", op);
    if (!equal(tok, op))
        error_tok(vm, tok, "expected '%s'", op);
    return tok->next;
}

bool consume(JCC *vm, Token **rest, Token *tok, char *str) {
    if (!tok) {
        *rest = NULL;
        return false;
    }
    if (equal(tok, str)) {
        *rest = tok->next;
        return true;
    }
    *rest = tok;
    return false;
}

// Create a new token.
static Token *new_token(JCC *vm, TokenKind kind, char *start, char *end) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = kind;
    tok->loc = start;
    tok->len = end - start;
    tok->file = vm->compiler.current_file;
    tok->filename = vm->compiler.current_file->display_name;
    tok->at_bol = vm->compiler.at_bol;
    tok->has_space = vm->compiler.has_space;

    vm->compiler.at_bol = vm->compiler.has_space = false;
    return tok;
}

static bool startswith(JCC *vm, char *p, char *q) {
    return strncmp(p, q, strlen(q)) == 0;
}

// Read an identifier and returns the length of it.
// If p does not point to a valid identifier, 0 is returned.
static int read_ident(JCC *vm, char *start) {
    char *p = start;
    uint32_t c = decode_utf8(vm, &p, p);
    if (!is_ident1(c))
        return 0;

    for (;;) {
        char *q;
        c = decode_utf8(vm, &q, p);
        if (!is_ident2(c))
            return p - start;
        p = q;
    }
}

static int from_hex(char c) {
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    return c - 'A' + 10;
}

// Read a punctuator token from p and returns its length.
static int read_punct(JCC *vm, char *p) {
    static char *kw[] = {
        "<<=", ">>=", "...", "==", "!=", "<=", ">=", "->", "+=",
        "-=", "*=", "/=", "++", "--", "%=", "&=", "|=", "^=", "&&",
        "||", "<<", ">>", "##",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (startswith(vm, p, kw[i]))
            return strlen(kw[i]);

    return ispunct(*p) ? 1 : 0;
}

static bool is_keyword(Token *tok) {
    static HashMap map;

    if (map.capacity == 0) {
        static char *kw[] = {
            "return", "if", "else", "for", "while", "int", "sizeof", "char",
            "struct", "union", "short", "long", "void", "typedef", "_Bool",
            "enum", "static", "goto", "break", "continue", "switch", "case",
            "default", "extern", "_Alignof", "_Alignas", "do", "signed",
            "unsigned", "const", "volatile", "auto", "register", "restrict",
            "__restrict", "__restrict__", "_Noreturn", "float", "double",
            "typeof", "typeof_unqual", "asm", "_Thread_local", "__thread", "_Atomic",
            "__attribute__", "_Static_assert", "static_assert", "constexpr",
            "__block", "_Complex", "_Imaginary",  // Apple Blocks extension and C99 complex
        };

        for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
            hashmap_put_borrowed(&map, kw[i], (void *)1);
    }

    return hashmap_get2(&map, tok->loc, tok->len);
}

static int read_escaped_char(JCC *vm, char **new_pos, char *p) {
    if ('0' <= *p && *p <= '7') {
        // Read an octal number.
        int c = *p++ - '0';
        if ('0' <= *p && *p <= '7') {
            c = (c << 3) + (*p++ - '0');
            if ('0' <= *p && *p <= '7')
                c = (c << 3) + (*p++ - '0');
        }
        *new_pos = p;
        return c;
    }

    if (*p == 'x') {
        // Read a hexadecimal number.
        p++;
        if (!isxdigit(*p))
            error_at(vm, p, "invalid hex escape sequence");

        int c = 0;
        for (; isxdigit(*p); p++)
            c = (c << 4) + from_hex(*p);
        *new_pos = p;
        return c;
    }

    *new_pos = p + 1;

    // Escape sequences are defined using themselves here. E.g.
    // '\n' is implemented using '\n'. This tautological definition
    // works because the compiler that compiles our compiler knows
    // what '\n' actually is. In other words, we "inherit" the ASCII
    // code of '\n' from the compiler that compiles our compiler,
    // so we don't have to teach the actual code here.
    //
    // This fact has huge implications not only for the correctness
    // of the compiler but also for the security of the generated code.
    // For more info, read "Reflections on Trusting Trust" by Ken Thompson.
    // https://github.com/rui314/chibicc/wiki/thompson1984.pdf
    switch (*p) {
        case 'a': return '\a';
        case 'b': return '\b';
        case 't': return '\t';
        case 'n': return '\n';
        case 'v': return '\v';
        case 'f': return '\f';
        case 'r': return '\r';
            // [GNU] \e for the ASCII escape character is a GNU C extension.
        case 'e': return 27;
        default: return *p;
    }
}

// Find a closing double-quote.
static char *string_literal_end(JCC *vm, char *p) {
    char *start = p;
    for (; *p != '"'; p++) {
        if (*p == '\n' || *p == '\0')
            error_at(vm, start, "unclosed string literal");
        if (*p == '\\')
            p++;
    }
    return p;
}

static Token *read_string_literal(JCC *vm, char *start, char *quote) {
    char *end = string_literal_end(vm, quote + 1);
    char *buf = arena_alloc(&vm->compiler.parser_arena, end - quote);
    memset(buf, 0, end - quote);
    int len = 0;

    for (char *p = quote + 1; p < end;) {
        if (*p == '\\')
            buf[len++] = read_escaped_char(vm, &p, p + 1);
        else
            buf[len++] = *p++;
    }

    Token *tok = new_token(vm, TK_STR, start, end + 1);
    tok->ty = array_of(vm, ty_char, len + 1);
    tok->str = buf;
    return tok;
}

// Read a UTF-8-encoded string literal and transcode it in UTF-16.
//
// UTF-16 is yet another variable-width encoding for Unicode. Code
// points smaller than U+10000 are encoded in 2 bytes. Code points
// equal to or larger than that are encoded in 4 bytes. Each 2 bytes
// in the 4 byte sequence is called "surrogate", and a 4 byte sequence
// is called a "surrogate pair".
static Token *read_utf16_string_literal(JCC *vm, char *start, char *quote) {
    char *end = string_literal_end(vm, quote + 1);
    uint16_t *buf = arena_alloc(&vm->compiler.parser_arena, 2 * (end - start));
    memset(buf, 0, 2 * (end - start));
    int len = 0;

    for (char *p = quote + 1; p < end;) {
        if (*p == '\\') {
            buf[len++] = read_escaped_char(vm, &p, p + 1);
            continue;
        }

        uint32_t c = decode_utf8(vm, &p, p);
        if (c < 0x10000) {
            // Encode a code point in 2 bytes.
            buf[len++] = c;
        } else {
            // Encode a code point in 4 bytes.
            c -= 0x10000;
            buf[len++] = 0xd800 + ((c >> 10) & 0x3ff);
            buf[len++] = 0xdc00 + (c & 0x3ff);
        }
    }

    Token *tok = new_token(vm, TK_STR, start, end + 1);
    tok->ty = array_of(vm, ty_ushort, len + 1);
    tok->str = (char *)buf;
    return tok;
}

// Read a UTF-8-encoded string literal and transcode it in UTF-32.
//
// UTF-32 is a fixed-width encoding for Unicode. Each code point is
// encoded in 4 bytes.
static Token *read_utf32_string_literal(JCC *vm, char *start, char *quote, Type *ty) {
    char *end = string_literal_end(vm, quote + 1);
    uint32_t *buf = arena_alloc(&vm->compiler.parser_arena, 4 * (end - quote));
    memset(buf, 0, 4 * (end - quote));
    int len = 0;

    for (char *p = quote + 1; p < end;) {
        if (*p == '\\')
            buf[len++] = read_escaped_char(vm, &p, p + 1);
        else
            buf[len++] = decode_utf8(vm, &p, p);
    }

    Token *tok = new_token(vm, TK_STR, start, end + 1);
    tok->ty = array_of(vm, ty, len + 1);
    tok->str = (char *)buf;
    return tok;
}

static Token *read_char_literal(JCC *vm, char *start, char *quote, Type *ty) {
    char *p = quote + 1;
    if (*p == '\0')
        error_at(vm, start, "unclosed char literal");

    int c;
    if (*p == '\\')
        c = read_escaped_char(vm, &p, p + 1);
    else
        c = decode_utf8(vm, &p, p);

    char *end = strchr(p, '\'');
    if (!end)
        error_at(vm, p, "unclosed char literal");

    Token *tok = new_token(vm, TK_NUM, start, end + 1);
    tok->val = c;
    tok->ty = ty;
    return tok;
}

static bool convert_pp_int(JCC *vm, Token *tok) {
    char *p = tok->loc;

    // Read a binary, octal, decimal or hexadecimal number.
    int base = 10;
    if (!strncasecmp(p, "0x", 2) && isxdigit(p[2])) {
        p += 2;
        base = 16;
    } else if (!strncasecmp(p, "0b", 2) && (p[2] == '0' || p[2] == '1')) {
        if (vm->compiler.c_std < JCC_STD_C23)
            error_tok(vm, tok, "binary integer literals are not available before C23");
        p += 2;
        base = 2;
    } else if (*p == '0') {
        base = 8;
    }

    // C23: Remove digit separators (single quotes) before parsing
    // e.g., 1'000'000 becomes 1000000
    char cleaned[256];
    int j = 0;
    for (char *s = p; *s && j < 255; s++) {
        if (*s == '\'') {
            if (vm->compiler.c_std < JCC_STD_C23)
                error_tok(vm, tok, "digit separators are not available before C23");
            continue;
        }
        // Stop at suffix (L, U, l, u) or non-alphanumeric
        if (!isalnum(*s))
            break;
        // Stop at suffix letters
        if ((*s == 'L' || *s == 'l' || *s == 'U' || *s == 'u') && j > 0)
            break;
        cleaned[j++] = *s;
    }
    cleaned[j] = '\0';

    int64_t val = strtoul(cleaned, &p, base);

    // Adjust p to point to the position in original string after digits
    // Count non-quote characters we consumed
    p = tok->loc + (base == 16 ? 2 : (base == 2 ? 2 : 0));
    for (int i = 0; i < j; ) {
        if (*p == '\'') {
            p++;  // Skip quote in original
        } else {
            p++;
            i++;
        }
    }

    // Read U, L or LL suffixes.
    bool l = false;
    bool u = false;

    if (startswith(vm, p, "LLU") || startswith(vm, p, "LLu") ||
        startswith(vm, p, "llU") || startswith(vm, p, "llu") ||
        startswith(vm, p, "ULL") || startswith(vm, p, "Ull") ||
        startswith(vm, p, "uLL") || startswith(vm, p, "ull")) {
        p += 3;
        l = u = true;
    } else if (!strncasecmp(p, "lu", 2) || !strncasecmp(p, "ul", 2)) {
        p += 2;
        l = u = true;
    } else if (startswith(vm, p, "LL") || startswith(vm, p, "ll")) {
        p += 2;
        l = true;
    } else if (*p == 'L' || *p == 'l') {
        p++;
        l = true;
    } else if (*p == 'U' || *p == 'u') {
        p++;
        u = true;
    }

    if (p != tok->loc + tok->len)
        return false;

    // Infer a type.
    Type *ty;
    if (base == 10) {
        if (l && u)
            ty = ty_ulong;
        else if (l)
            ty = ty_long;
        else if (u)
            ty = (val >> 32) ? ty_ulong : ty_uint;
        else
            ty = (val >> 31) ? ty_long : ty_int;
    } else {
        if (l && u)
            ty = ty_ulong;
        else if (l)
            ty = (val >> 63) ? ty_ulong : ty_long;
        else if (u)
            ty = (val >> 32) ? ty_ulong : ty_uint;
        else if (val >> 63)
            ty = ty_ulong;
        else if (val >> 32)
            ty = ty_long;
        else if (val >> 31)
            ty = ty_uint;
        else
            ty = ty_int;
    }

    tok->kind = TK_NUM;
    tok->val = val;
    tok->ty = ty;
    return true;
}

// The definition of the numeric literal at the preprocessing stage
// is more relaxed than the definition of that at the later stages.
// In order to handle that, a numeric literal is tokenized as a
// "pp-number" token first and then converted to a regular number
// token after preprocessing.
//
// This function converts a pp-number token to a regular number token.
static void convert_pp_number(JCC *vm, Token *tok) {
    // Try to parse as an integer constant.
    if (convert_pp_int(vm, tok))
        return;

    // If it's not an integer, it must be a floating point constant.
    char *end;
    long double val = strtold(tok->loc, &end);

    Type *ty;
    if (*end == 'f' || *end == 'F') {
        ty = ty_float;
        end++;
    } else if (*end == 'l' || *end == 'L') {
        ty = ty_ldouble;
        end++;
    } else {
        ty = ty_double;
    }

    if (tok->loc + tok->len != end)
        error_tok(vm, tok, "invalid numeric constant");

    tok->kind = TK_NUM;
    tok->fval = val;
    tok->ty = ty;
}

// Check whether a newly-recognised keyword is available in the selected -std.
// Returns true if the keyword is allowed; on false the caller should downgrade
// the token to TK_IDENT.  Emits an error_tok for reserved-name violations.
// Tokens from macro expansion (t->origin != NULL) are exempt — those come from
// header macros that abstract over standard versions.
static bool keyword_std_ok(JCC *vm, Token *t) {
    if (t->origin)
        return true;
    CStdVersion s = vm->compiler.c_std;
    int len = t->len;
    char *kw = t->loc;

#define KW(str) (len == (int)sizeof(str)-1 && memcmp(kw, str, len) == 0)

    // C99 reserved names — illegal below C99 (fire when -std=c89 is selected)
    if (s < JCC_STD_C99) {
        if (KW("restrict") || KW("__restrict") || KW("__restrict__"))
            error_tok(vm, t, "'%.*s' is not available before C99", len, kw);
        if (KW("_Bool"))
            error_tok(vm, t, "'_Bool' is not available before C99");
        if (KW("_Complex") || KW("_Imaginary"))
            error_tok(vm, t, "'%.*s' is not available before C99", len, kw);
    }

    // C11 reserved names — illegal below C11
    if (s < JCC_STD_C11) {
        if (KW("_Alignof") || KW("_Alignas"))
            error_tok(vm, t, "'%.*s' is not available before C11", len, kw);
        if (KW("_Noreturn"))
            error_tok(vm, t, "'_Noreturn' is not available before C11");
        if (KW("_Thread_local") || KW("__thread"))
            error_tok(vm, t, "'%.*s' is not available before C11", len, kw);
        if (KW("_Atomic"))
            error_tok(vm, t, "'_Atomic' is not available before C11");
        if (KW("_Static_assert"))
            error_tok(vm, t, "'_Static_assert' is not available before C11");
    }

    // C23 keywords that were valid identifiers in earlier standards — downgrade
    if (s < JCC_STD_C23) {
        if (KW("constexpr") || KW("static_assert"))
            return false;
    }

#undef KW
    return true;
}

void convert_pp_tokens(JCC *vm, Token *tok) {
    for (Token *t = tok; t->kind != TK_EOF; t = t->next) {
        if (is_keyword(t)) {
            t->kind = TK_KEYWORD;
            if (!keyword_std_ok(vm, t))
                t->kind = TK_IDENT;
        } else if (t->kind == TK_PP_NUM)
            convert_pp_number(vm, t);
    }
}

// Initialize line info for all tokens.
static void add_line_numbers(JCC *vm, Token *tok) {
    char *p = vm->compiler.current_file->contents;
    char *line_start = p;
    int n = 1;

    for (Token *t = tok; ; t = t->next) {
        while (p < t->loc) {
            if (*p == '\n') {
                n++;
                line_start = p + 1;
            }
            p++;
        }
        t->line_no = n;
        t->col_no = display_width(vm, line_start, t->loc - line_start) + 1;
        if (t->kind == TK_EOF)
            break;
    }
}

Token *tokenize_string_literal(JCC *vm, Token *tok, Type *basety) {
    Token *t;
    if (basety->size == 2)
        t = read_utf16_string_literal(vm, tok->loc, tok->loc);
    else
        t = read_utf32_string_literal(vm, tok->loc, tok->loc, basety);
    t->next = tok->next;
    return t;
}

// Tokenize a given string and returns new tokens.
Token *tokenize(JCC *vm, File *file) {
    vm->compiler.current_file = file;

    char *p = file->contents;
    Token head = {};
    Token *cur = &head;

    vm->compiler.at_bol = true;
    vm->compiler.has_space = false;

    // State tracking for #include directive to preserve // in URLs
    bool after_include_directive = false;  // True after we see #include
    bool in_include_path = false;          // True when inside <...> or "..." of #include

    while (*p) {
        // Skip line comments (but NOT inside #include paths where URLs may contain //)
        if (startswith(vm, p, "//") && !in_include_path) {
            if (vm->compiler.c_std < JCC_STD_C99)
                error_at(vm, p, "line comments are not available before C99");
            p += 2;
            while (*p != '\n')
                p++;
            vm->compiler.has_space = true;
            continue;
        }

        // Skip block comments (also not inside #include paths)
        if (startswith(vm, p, "/*") && !in_include_path) {
            char *q = strstr(p + 2, "*/");
            if (!q)
                error_at(vm, p, "unclosed block comment");
            p = q + 2;
            vm->compiler.has_space = true;
            continue;
        }

        // Skip newline.
        if (*p == '\n') {
            p++;
            vm->compiler.at_bol = true;
            vm->compiler.has_space = false;
            // Reset include directive state on newline
            after_include_directive = false;
            in_include_path = false;
            continue;
        }

        // Skip whitespace characters.
        if (isspace(*p)) {
            p++;
            vm->compiler.has_space = true;
            continue;
        }

        // Numeric literal
        if (isdigit(*p) || (*p == '.' && isdigit(p[1]))) {
            char *q = p++;
            for (;;) {
                if (p[0] && p[1] && strchr("eEpP", p[0]) && strchr("+-", p[1]))
                    p += 2;
                else if (isalnum(*p) || *p == '.' || *p == '\'')  // C23: digit separators
                    p++;
                else
                    break;
            }
            cur = cur->next = new_token(vm, TK_PP_NUM, q, p);
            continue;
        }

        // String literal
        if (*p == '"') {
            cur = cur->next = read_string_literal(vm, p, p);
            p += cur->len;
            continue;
        }

        // UTF-8 string literal
        if (startswith(vm, p, "u8\"")) {
            cur = cur->next = read_string_literal(vm, p, p + 2);
            p += cur->len;
            continue;
        }

        // UTF-16 string literal
        if (startswith(vm, p, "u\"")) {
            cur = cur->next = read_utf16_string_literal(vm, p, p + 1);
            p += cur->len;
            continue;
        }

        // Wide string literal
        if (startswith(vm, p, "L\"")) {
            cur = cur->next = read_utf32_string_literal(vm, p, p + 1, ty_int);
            p += cur->len;
            continue;
        }

        // UTF-32 string literal
        if (startswith(vm, p, "U\"")) {
            cur = cur->next = read_utf32_string_literal(vm, p, p + 1, ty_uint);
            p += cur->len;
            continue;
        }

        // Character literal
        if (*p == '\'') {
            cur = cur->next = read_char_literal(vm, p, p, ty_int);
            cur->val = (char)cur->val;
            p += cur->len;
            continue;
        }

        // UTF-16 character literal
        if (startswith(vm, p, "u'")) {
            cur = cur->next = read_char_literal(vm, p, p + 1, ty_ushort);
            cur->val &= 0xffff;
            p += cur->len;
            continue;
        }

        // Wide character literal
        if (startswith(vm, p, "L'")) {
            cur = cur->next = read_char_literal(vm, p, p + 1, ty_int);
            p += cur->len;
            continue;
        }

        // UTF-32 character literal
        if (startswith(vm, p, "U'")) {
            cur = cur->next = read_char_literal(vm, p, p + 1, ty_uint);
            p += cur->len;
            continue;
        }

        // Detect # at beginning of line (potential directive)
        if (vm->compiler.at_bol && *p == '#') {
            // Check if this is #include by peeking ahead
            char *peek = p + 1;
            while (isspace(*peek) && *peek != '\n') peek++;
            if (strncmp(peek, "include", 7) == 0 &&
                (peek[7] == '\0' || (!isalnum(peek[7]) && peek[7] != '_'))) {
                after_include_directive = true;
            }
        }

        // Track entering/exiting path in #include directive
        // Handle both #include <...> and #include "..."
        if (after_include_directive) {
            if (*p == '<' || *p == '"') {
                in_include_path = true;
                after_include_directive = false;  // Clear the flag
            }
        }
        if (in_include_path) {
            if (*p == '>' || *p == '"') {
                in_include_path = false;  // This char closes the path
            }
        }

        // Identifier or keyword
        int ident_len = read_ident(vm, p);
        if (ident_len) {
            cur = cur->next = new_token(vm, TK_IDENT, p, p + ident_len);
            p += cur->len;
            // Check if we just tokenized "include" after #
            if (after_include_directive && ident_len == 7 && strncmp(p - ident_len, "include", 7) == 0) {
                // Keep the flag set, we'll look for < or " next
            }
            continue;
        }

        // Punctuators
        int punct_len = read_punct(vm, p);
        if (punct_len) {
            cur = cur->next = new_token(vm, TK_PUNCT, p, p + punct_len);
            p += cur->len;
            continue;
        }

        error_at(vm, p, "invalid token");
    }

    cur = cur->next = new_token(vm, TK_EOF, p, p);
    add_line_numbers(vm, head.next);
    return head.next;
}

// Returns the contents of a given file.
static char *read_file(JCC *vm, char *path) {
    FILE *fp;

    if (strncmp(path, "-", sizeof("-")) == 0) {
        // By convention, read from stdin if a given filename is "-".
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp)
            return NULL;
    }

    char *buf;
    size_t buflen;
    FILE *out = open_memstream(&buf, &buflen);

    // Read the entire file.
    for (;;) {
        char buf2[4096];
        int n = fread(buf2, 1, sizeof(buf2), fp);
        if (n == 0)
            break;
        fwrite(buf2, 1, n, out);
    }

    if (fp != stdin)
        fclose(fp);

    // Make sure that the last line is properly terminated with '\n'.
    fflush(out);
    if (buflen == 0 || buf[buflen - 1] != '\n')
        fputc('\n', out);
    fputc('\0', out);
    fclose(out);

    // Register buffer for cleanup
    strarray_push(&vm->compiler.file_buffers, buf);

    return buf;
}

// Read binary file without text processing (for #embed directive)
unsigned char *read_binary_file(JCC *vm, char *path, size_t *out_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    // Get file size
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    *out_size = (size_t)file_size;

    // Allocate buffer (use arena for automatic cleanup)
    unsigned char *buffer = arena_alloc(&vm->compiler.parser_arena, file_size);

    // Read entire file
    if (file_size > 0) {
        size_t bytes_read = fread(buffer, 1, file_size, fp);
        if (bytes_read != (size_t)file_size) {
            fclose(fp);
            error("failed to read file: %s", path);
        }
    }

    fclose(fp);
    return buffer;
}

File *new_file(JCC *vm, char *name, int file_no, char *contents) {
    File *file = arena_alloc(&vm->compiler.parser_arena, sizeof(File));
    memset(file, 0, sizeof(File));
    file->name = name;
    file->display_name = name;
    file->file_no = file_no;
    file->contents = contents;
    return file;
}

// Replaces \r or \r\n with \n.
static void canonicalize_newline(char *p) {
    int i = 0, j = 0;

    while (p[i]) {
        if (p[i] == '\r' && p[i + 1] == '\n') {
            i += 2;
            p[j++] = '\n';
        } else if (p[i] == '\r') {
            i++;
            p[j++] = '\n';
        } else {
            p[j++] = p[i++];
        }
    }

    p[j] = '\0';
}

// Removes backslashes followed by a newline.
static void remove_backslash_newline(char *p) {
    int i = 0, j = 0;

    // We want to keep the number of newline characters so that
    // the logical line number matches the physical one.
    // This counter maintain the number of newlines we have removed.
    int n = 0;

    while (p[i]) {
        if (p[i] == '\\' && p[i + 1] == '\n') {
            i += 2;
            n++;
        } else if (p[i] == '\n') {
            p[j++] = p[i++];
            for (; n > 0; n--)
                p[j++] = '\n';
        } else {
            p[j++] = p[i++];
        }
    }

    for (; n > 0; n--)
        p[j++] = '\n';
    p[j] = '\0';
}

static uint32_t read_universal_char(char *p, int len) {
    uint32_t c = 0;
    for (int i = 0; i < len; i++) {
        if (!isxdigit(p[i]))
            return 0;
        c = (c << 4) | from_hex(p[i]);
    }
    return c;
}

// Replace \u or \U escape sequences with corresponding UTF-8 bytes.
static void convert_universal_chars(JCC *vm, char *p) {
    char *q = p;

    while (*p) {
        if (startswith(vm, p, "\\u")) {
            uint32_t c = read_universal_char(p + 2, 4);
            if (c) {
                p += 6;
                q += encode_utf8(q, c);
            } else {
                *q++ = *p++;
            }
        } else if (startswith(vm, p, "\\U")) {
            uint32_t c = read_universal_char(p + 2, 8);
            if (c) {
                p += 10;
                q += encode_utf8(q, c);
            } else {
                *q++ = *p++;
            }
        } else if (p[0] == '\\') {
            *q++ = *p++;
            *q++ = *p++;
        } else {
            *q++ = *p++;
        }
    }

    *q = '\0';
}

Token *tokenize_file(JCC *vm, char *path) {
    char *p = read_file(vm, path);
    if (!p)
        return NULL;

    // UTF-8 texts may start with a 3-byte "BOM" marker sequence.
    // If exists, just skip them because they are useless bytes.
    // (It is actually not recommended to add BOM markers to UTF-8
    // texts, but it's not uncommon particularly on Windows.)
    if (!memcmp(p, "\xef\xbb\xbf", 3))
        p += 3;

    canonicalize_newline(p);
    remove_backslash_newline(p);
    convert_universal_chars(vm, p);

    // Save the filename for assembler .file directive.
    static int file_no;
    File *file = new_file(vm, path, file_no + 1, p);

    // Save the filename for assembler .file directive.
    vm->compiler.input_files = realloc(vm->compiler.input_files, sizeof(char *) * (file_no + 2));
    vm->compiler.input_files[file_no] = file;
    vm->compiler.input_files[file_no + 1] = NULL;
    file_no++;

    return tokenize(vm, file);
}

// Tokenize an in-memory string (for embedded headers)
Token *tokenize_string(JCC *vm, char *name, char *contents) {
    // Duplicate contents because tokenize may modify it
    char *p = arena_alloc(&vm->compiler.parser_arena, strlen(contents) + 1);
    strcpy(p, contents);

    canonicalize_newline(p);
    remove_backslash_newline(p);
    convert_universal_chars(vm, p);

    static int embedded_file_no;
    File *file = new_file(vm, name, -(embedded_file_no + 1), p);
    embedded_file_no++;

    return tokenize(vm, file);
}

// Output preprocessed tokens as source code (for -E flag)
void cc_output_preprocessed(FILE *f, Token *tok) {
    if (!f || !tok)
        return;
    
    int at_bol = 1;
    
    for (Token *t = tok; t && t->kind != TK_EOF; t = t->next) {
        // Handle line breaks
        if (at_bol && !t->at_bol) {
            // Continue on same line
        } else if (t->at_bol && !at_bol) {
            fprintf(f, "\n");
        }
        at_bol = t->at_bol;
        
        // Handle spacing
        if (t->has_space && !at_bol)
            fprintf(f, " ");
        
        // Output token based on kind
        switch (t->kind) {
            case TK_IDENT:
            case TK_PP_NUM:
                fprintf(f, "%.*s", t->len, t->loc);
                break;
                
            case TK_NUM:
                // For numeric literals, output the original text
                if (t->ty && is_flonum(t->ty)) {
                    fprintf(f, "%.*s", t->len, t->loc);
                } else {
                    fprintf(f, "%.*s", t->len, t->loc);
                }
                break;
                
            case TK_STR:
                // String literals: output with quotes
                fprintf(f, "%.*s", t->len, t->loc);
                break;
                
            case TK_KEYWORD:
                fprintf(f, "%.*s", t->len, t->loc);
                break;
                
            case TK_EOF:
                break;
                
            default:
                // For all other tokens (operators, punctuation, etc.)
                fprintf(f, "%.*s", t->len, t->loc);
                break;
        }
        
        at_bol = 0;
    }

    fprintf(f, "\n");
}

// Error collection helper functions

int cc_get_error_count(JCC *vm) {
    return vm ? vm->error_count : 0;
}

int cc_get_warning_count(JCC *vm) {
    return vm ? vm->warning_count : 0;
}

bool cc_has_errors(JCC *vm) {
    return vm && vm->error_count > 0;
}

void cc_clear_errors(JCC *vm) {
    if (!vm) return;

    vm->errors = NULL;
    vm->errors_tail = NULL;
    vm->error_count = 0;
    vm->warning_count = 0;
    vm->error_message = NULL;
}

void cc_print_all_errors(JCC *vm) {
    if (!vm || !vm->errors) return;

    // Print all collected errors and warnings
    CompileError *err = vm->errors;
    int error_num = 0;
    int warning_num = 0;

    while (err) {
        fprintf(stderr, "%s", err->message);
        if (err->severity == 0) {
            error_num++;
        } else {
            warning_num++;
        }
        err = err->next;
    }

    // Print summary
    if (error_num > 0 || warning_num > 0) {
        fprintf(stderr, "\n");
        if (error_num > 0 && warning_num > 0) {
            fprintf(stderr, "%d error%s and %d warning%s generated.\n",
                    error_num, error_num == 1 ? "" : "s",
                    warning_num, warning_num == 1 ? "" : "s");
        } else if (error_num > 0) {
            fprintf(stderr, "%d error%s generated.\n",
                    error_num, error_num == 1 ? "" : "s");
        } else {
            fprintf(stderr, "%d warning%s generated.\n",
                    warning_num, warning_num == 1 ? "" : "s");
        }
    }
}
