/*
 CCCC: Comprehensiev C Compensation Compiler

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

#include "./internal.h"
#include <pthread.h>

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
    {"unused", CCCC_WARN_UNUSED, false},
    {"implicit-function-declaration", CCCC_WARN_IMPLICIT_FUNCTION_DECLARATION, false},
    {"implicit-int", CCCC_WARN_IMPLICIT_INT, false},
    {"return-type", CCCC_WARN_RETURN_TYPE, false},
    {"shadow", CCCC_WARN_SHADOW, false},
    {"format", CCCC_WARN_FORMAT, false},
    // -Wconversion is the umbrella: enables integer narrowing, sign-conversion,
    // and float-conversion.  The first entry maps the name to the full group
    // mask so -Wconversion / -Wno-conversion / -Werror=conversion all cover all
    // three sub-categories.  The second (non-group) entry exists only so that
    // cccc_warning_name(CCCC_WARN_CONVERSION) (exact-bit match) still returns
    // "conversion" for the [-Wconversion] tag in integer-narrowing diagnostics.
    {"conversion", CCCC_WARN_CONVERSION_GROUP, false},
    {"conversion", CCCC_WARN_CONVERSION, false},
    {"sign-conversion", CCCC_WARN_SIGN_CONVERSION, false},
    {"float-conversion", CCCC_WARN_FLOAT_CONVERSION, false},
    {"sign-compare", CCCC_WARN_SIGN_COMPARE, false},
    {"pointer-arith", CCCC_WARN_POINTER_ARITH, false},
    {"pedantic", CCCC_WARN_PEDANTIC, false},
    {"deprecated", CCCC_WARN_DEPRECATED, false},
    {"cpp", CCCC_WARN_CPP, false},
    {"extra-tokens", CCCC_WARN_EXTRA_TOKENS, false},
    {"large-file-embed", CCCC_WARN_LARGE_FILE_EMBED, false},
    {"cccc-macro", CCCC_WARN_CCCC_MACRO, false},
    {"comptime-block-leak", CCCC_WARN_COMPTIME_BLOCK_LEAK, false},
    {"ignored-features", CCCC_WARN_IGNORED_FEATURES, false},
    {"attributes", CCCC_WARN_ATTRIBUTES, false},
    {"nodiscard", CCCC_WARN_NODISCARD, false},
    {"fallthrough", CCCC_WARN_FALLTHROUGH, false},
    {"static-array-size", CCCC_WARN_STATIC_ARRAY_SIZE, false},
    {"strict-prototypes", CCCC_WARN_STRICT_PROTOTYPES, false},
    {"discarded-qualifiers", CCCC_WARN_DISCARDED_QUALIFIERS, false},
    {"null-dereference",     CCCC_WARN_NULL_DEREFERENCE,    false},
    {"restrict",             CCCC_WARN_RESTRICT,             false},
    {"array-bounds",         CCCC_WARN_ARRAY_BOUNDS,         false},
    {"stringop-overflow",    CCCC_WARN_STRINGOP_OVERFLOW,    false},
    {"stringop-truncation",  CCCC_WARN_STRINGOP_TRUNCATION,  false},
    {"duplicated-branches",  CCCC_WARN_DUPLICATED_BRANCHES,  false},
    {"duplicated-cond",      CCCC_WARN_DUPLICATED_COND,      false},
    {"unused-value",         CCCC_WARN_UNUSED_VALUE,         false},
    {"multichar",            CCCC_WARN_MULTICHAR,            false},
    {"main",                 CCCC_WARN_MAIN,                 false},
    {"switch-default",       CCCC_WARN_SWITCH_DEFAULT,       false},
    {"switch-bool",          CCCC_WARN_SWITCH_BOOL,          false},
    {"float-equal",              CCCC_WARN_FLOAT_EQUAL,              false},
    {"shift-negative-value",     CCCC_WARN_SHIFT_NEGATIVE_VALUE,     false},
    {"shift-overflow",           CCCC_WARN_SHIFT_OVERFLOW,           false},
    {"logical-op",               CCCC_WARN_LOGICAL_OP,               false},
    {"tautological-compare",     CCCC_WARN_TAUTOLOGICAL_COMPARE,     false},
    {"sizeof-pointer-memaccess", CCCC_WARN_SIZEOF_POINTER_MEMACCESS, false},
    {"switch",                  CCCC_WARN_SWITCH,                  false},
    {"switch-enum",             CCCC_WARN_SWITCH_ENUM,             false},
    {"enum-compare",            CCCC_WARN_ENUM_COMPARE,            false},
    {"old-style-definition",    CCCC_WARN_OLD_STYLE_DEFINITION,    false},
    {"incompatible-pointer-types", CCCC_WARN_INCOMPATIBLE_POINTER_TYPES, false},
    {"cast-qual",                  CCCC_WARN_CAST_QUAL,                  false},
    {"cast-align",                 CCCC_WARN_CAST_ALIGN,                 false},
    {"missing-prototypes",         CCCC_WARN_MISSING_PROTOTYPES,         false},
    {"missing-declarations",       CCCC_WARN_MISSING_DECLARATIONS,       false},
    {"redundant-decls",            CCCC_WARN_REDUNDANT_DECLS,            false},
    {"override-init",              CCCC_WARN_OVERRIDE_INIT,              false},
    {"unused-macros",              CCCC_WARN_UNUSED_MACROS,              false},
    {"nonnull",                    CCCC_WARN_NONNULL,                    false},
    {"maybe-nonnull",              CCCC_WARN_MAYBE_NONNULL,              false},
    {"sentinel",                   CCCC_WARN_SENTINEL,                   false},
    {"designated-init",            CCCC_WARN_DESIGNATED_INIT,            false},
    {"all", CCCC_WARN_ALL, true},
    {"extra", CCCC_WARN_EXTRA, true},
};

const char *cccc_warning_name(CCCCWarning warning) {
    uint64_t mask = (uint64_t)warning;
    for (size_t i = 0; i < sizeof(warning_infos) / sizeof(*warning_infos); i++)
        if (!warning_infos[i].is_group && warning_infos[i].mask == mask)
            return warning_infos[i].name;
    return NULL;
}

uint64_t cccc_warning_mask_for_name(const char *name) {
    for (size_t i = 0; i < sizeof(warning_infos) / sizeof(*warning_infos); i++)
        if (strcmp(warning_infos[i].name, name) == 0)
            return warning_infos[i].mask;
    return 0;
}

bool cccc_warning_is_group_name(const char *name) {
    for (size_t i = 0; i < sizeof(warning_infos) / sizeof(*warning_infos); i++)
        if (strcmp(warning_infos[i].name, name) == 0)
            return warning_infos[i].is_group;
    return false;
}

// Reports a diagnostic message in the following format.
//
// foo.c:10: x = y + 1;
static void print_escaped_string_fp(FILE *f, const char *s) {
    for (; *s; s++) {
        switch (*s) {
        case '"':  fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\n': fputs("\\n",  f); break;
        case '\r': fputs("\\r",  f); break;
        case '\t': fputs("\\t",  f); break;
        default:
            if ((unsigned char)*s < 0x20)
                fprintf(f, "\\u%04x", (unsigned char)*s);
            else
                fputc(*s, f);
        }
    }
}

//               ^ error: <message here>
static void vdiagnostic_at(VirtualMachine *vm, char *filename, char *input, int line_no,
                           char *loc, const char *kind,
                           const char *warn_name, char *fmt, va_list ap) {
    // Guard: loc must be within the file's contents buffer.
    // Synthesized tokens (e.g. incremental $@k placeholders) may have their
    // loc rewritten to arena memory; clamp to input start so display_width
    // does not scan out-of-bounds arena bytes.
    char *file_end = input + strlen(input);
    if (loc < input || loc > file_end)
        loc = input;

    // Find a line containing `loc`.
    char *line = loc;
    while (input < line && line[-1] != '\n')
        line--;

    char *end = loc;
    while (*end && *end != '\n')
        end++;

    int col_no = (int)(loc - line) + 1;

    // JSON diagnostic mode: emit one JSON object per diagnostic, no buffering.
    if (vm && vm->compiler.diagnostic_json) {
        char plain_msg[4096];
        va_list ap2;
        va_copy(ap2, ap);
        vsnprintf(plain_msg, sizeof(plain_msg), fmt, ap2);
        va_end(ap2);
        fprintf(stderr, "{\"severity\":\"%s\",\"file\":\"", kind);
        print_escaped_string_fp(stderr, filename);
        fprintf(stderr, "\",\"line\":%d,\"column\":%d,\"message\":\"", line_no, col_no);
        print_escaped_string_fp(stderr, plain_msg);
        if (warn_name)
            fprintf(stderr, "\",\"option\":\"-W%s\"}\n", warn_name);
        else
            fprintf(stderr, "\",\"option\":null}\n");
        vm->error_message = NULL;
        return;
    }

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

static void verror_at(VirtualMachine *vm, char *filename, char *input, int line_no,
                       char *loc, char *fmt, va_list ap) {
    vdiagnostic_at(vm, filename, input, line_no, loc, "error", NULL, fmt, ap);
}

// Compute line number from a token's location if it hasn't been set yet.
// This handles tokens created before add_line_numbers() runs (e.g. errors
// during tokenize()).
static int tok_line_no(Token *tok) {
    if (tok->line_no > 0)
        return tok->line_no;
    if (!tok->file || !tok->file->contents || !tok->loc)
        return 0;
    int line_no = 1;
    for (char *p = tok->file->contents; p < tok->loc; p++)
        if (*p == '\n')
            line_no++;
    return line_no;
}

static int tok_col_no(Token *tok) {
    if (tok->col_no > 0)
        return tok->col_no;
    if (!tok->file || !tok->file->contents || !tok->loc)
        return 0;
    char *line_start = tok->loc;
    while (line_start > tok->file->contents && line_start[-1] != '\n')
        line_start--;
    return (int)(tok->loc - line_start) + 1;
}

void error_at(VirtualMachine *vm, char *loc, char *fmt, ...) {
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

void error_tok(VirtualMachine *vm, Token *tok, char *fmt, ...) {
    int line_no = tok_line_no(tok);
    int col_no = tok_col_no(tok);

    va_list ap;
    va_start(ap, fmt);
    verror_at(vm, tok->file->name, tok->file->contents, line_no, tok->loc, fmt, ap);
    va_end(ap);

    // Collect error if error collection is enabled
    if (vm && vm->collect_errors && vm->error_message) {
        CompileError *err = arena_alloc(&vm->compiler.parser_arena, sizeof(CompileError));
        err->message = vm->error_message;
        err->filename = tok->file->name;
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

// Error reporting with recovery support (Level 2)
// Returns true if parsing should continue with recovery, false if max errors hit
bool error_tok_recover(VirtualMachine *vm, Token *tok, char *fmt, ...) {
    int line_no = tok_line_no(tok);
    int col_no = tok_col_no(tok);

    va_list ap;
    va_start(ap, fmt);
    verror_at(vm, tok->file->name, tok->file->contents, line_no, tok->loc, fmt, ap);
    va_end(ap);

    // Collect error if error collection is enabled
    if (vm && vm->collect_errors && vm->error_message) {
        CompileError *err = arena_alloc(&vm->compiler.parser_arena, sizeof(CompileError));
        err->message = vm->error_message;
        err->filename = tok->file->name;
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

void warn_tok(VirtualMachine *vm, Token *tok, CCCCWarning category, char *fmt, ...) {
    uint64_t mask = (uint64_t)category;

    // Use per-token effective state if the preprocessor stamped it; otherwise
    // fall back to the global compiler warning state.
    uint64_t eff_warnings = (tok && tok->diag_warnings)
        ? (tok->diag_warnings & ~(1ULL << 63))
        : (vm ? vm->compiler.warnings : 0);
    uint64_t eff_werror   = (tok && tok->diag_werror)
        ? (tok->diag_werror & ~(1ULL << 63))
        : (vm ? vm->compiler.warning_errors : 0);

    if (!vm || !(eff_warnings & mask))
        return;

    int line_no = tok_line_no(tok);
    int col_no = tok_col_no(tok);

    const char *warn_name = cccc_warning_name(category);
    bool is_error = (vm->warnings_as_errors &&
                     !(vm->compiler.warning_no_errors & mask)) ||
                    (eff_werror & mask);

    va_list ap;
    va_start(ap, fmt);
    vdiagnostic_at(vm, tok->file->name, tok->file->contents, line_no,
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
            err->line_no = line_no;
            err->col_no = col_no;
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
        err->line_no = line_no;
        err->col_no = col_no;
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

void warn_at(VirtualMachine *vm, char *loc, CCCCWarning category, char *fmt, ...) {
    uint64_t mask = (uint64_t)category;

    if (!vm || !(vm->compiler.warnings & mask))
        return;

    int line_no = 1;
    for (char *p = vm->compiler.current_file->contents; p < loc; p++)
        if (*p == '\n')
            line_no++;

    int col_no = 1;
    char *line_start = loc;
    while (line_start > vm->compiler.current_file->contents &&
           line_start[-1] != '\n') {
        line_start--;
        col_no++;
    }

    const char *warn_name = cccc_warning_name(category);
    bool is_error = (vm->warnings_as_errors &&
                     !(vm->compiler.warning_no_errors & mask)) ||
                    (vm->compiler.warning_errors & mask);

    va_list ap;
    va_start(ap, fmt);
    vdiagnostic_at(vm, vm->compiler.current_file->name,
                   vm->compiler.current_file->contents, line_no, loc,
                   is_error ? "error" : "warning", warn_name, fmt, ap);
    va_end(ap);

    if (vm->error_jmp_buf && !vm->collect_errors && !is_error &&
        vm->error_message) {
        fprintf(stderr, "%s", vm->error_message);
        vm->error_message = NULL;
    }

    if (is_error) {
        if (vm->error_message) {
            CompileError *err =
                arena_alloc(&vm->compiler.parser_arena, sizeof(CompileError));
            err->message = vm->error_message;
            err->filename = vm->compiler.current_file->name;
            err->line_no = line_no;
            err->col_no = col_no;
            err->severity = 0;
            err->warn_name = warn_name;
            err->next = NULL;

            if (!vm->errors) {
                vm->errors = vm->errors_tail = err;
            } else {
                vm->errors_tail->next = err;
                vm->errors_tail = err;
            }
            vm->error_count++;
            vm->error_message = NULL;
        }

        if (vm->error_jmp_buf)
            longjmp(*vm->error_jmp_buf, 1);
        exit(1);
    }

    if (vm->collect_errors && vm->error_message) {
        CompileError *err =
            arena_alloc(&vm->compiler.parser_arena, sizeof(CompileError));
        err->message = vm->error_message;
        err->filename = vm->compiler.current_file->name;
        err->line_no = line_no;
        err->col_no = col_no;
        err->severity = 1;
        err->warn_name = warn_name;
        err->next = NULL;

        if (!vm->errors) {
            vm->errors = vm->errors_tail = err;
        } else {
            vm->errors_tail->next = err;
            vm->errors_tail = err;
        }
        vm->warning_count++;
        vm->error_message = NULL;
    }
}

// Returns the canonical single/double-char spelling for a C digraph, or NULL.
// Digraphs (C23 §6.4.6): %:%:→## <:→[ :>→] <%→{ %>→} %:→#
static const char *digraph_canonical(char *loc, int len) {
    if (len == 4 && memcmp(loc, "%:%:", 4) == 0) return "##";
    if (len == 2) {
        if (memcmp(loc, "<:", 2) == 0) return "[";
        if (memcmp(loc, ":>", 2) == 0) return "]";
        if (memcmp(loc, "<%", 2) == 0) return "{";
        if (memcmp(loc, "%>", 2) == 0) return "}";
        if (memcmp(loc, "%:", 2) == 0) return "#";
    }
    return NULL;
}

// Consumes the current token if it matches `op`.
bool equal(Token *tok, char *op) {
    if (!tok) return false;
    int oplen = strlen(op);
    const char *canon = digraph_canonical(tok->loc, tok->len);
    if (canon)
        return oplen == (int)strlen(canon) && memcmp(canon, op, oplen) == 0;
    return oplen == tok->len && memcmp(tok->loc, op, tok->len) == 0;
}

// Ensure that the current token is `op`.
Token *skip(VirtualMachine *vm, Token *tok, char *op) {
    if (!tok)
        error("expected '%s'", op);
    if (!equal(tok, op))
        error_tok(vm, tok, "expected '%s'", op);
    return tok->next;
}

bool consume(VirtualMachine *vm, Token **rest, Token *tok, char *str) {
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
static Token *new_token(VirtualMachine *vm, TokenKind kind, char *start, char *end) {
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

static bool startswith(VirtualMachine *vm, char *p, char *q) {
    return strncmp(p, q, strlen(q)) == 0;
}

// Read an identifier and returns the length of it.
// If p does not point to a valid identifier, 0 is returned.
static int read_ident(VirtualMachine *vm, char *start) {
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
static int read_punct(VirtualMachine *vm, char *p) {
    static char *kw[] = {
        // Digraphs (C23 §6.4.6) — must precede any operator they overlap with
        "%:%:", "<:", ":>", "<%", "%>", "%:",
        "<<=", ">>=", "...", "==", "!=", "<=", ">=", "->", "+=",
        "-=", "*=", "/=", "++", "--", "%=", "&=", "|=", "^=", "&&",
        "||", "<<", ">>", "##",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (startswith(vm, p, kw[i]))
            return strlen(kw[i]);

    return ispunct(*p) ? 1 : 0;
}

static HashMap keyword_map;
static pthread_once_t keyword_map_once = PTHREAD_ONCE_INIT;

static void init_keyword_map(void) {
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
        "bool", "true", "false", "nullptr", "thread_local",  // C23 keywords
        "_BitInt", "_Decimal32", "_Decimal64", "_Decimal128",  // C23 types
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        hashmap_put_borrowed(&keyword_map, kw[i], (void *)1);
}

static bool is_keyword(Token *tok) {
    pthread_once(&keyword_map_once, init_keyword_map);
    return hashmap_get2(&keyword_map, tok->loc, tok->len);
}

static int read_escaped_char(VirtualMachine *vm, char **new_pos, char *p) {
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
static char *string_literal_end(VirtualMachine *vm, char *p) {
    char *start = p;
    for (; *p != '"'; p++) {
        if (*p == '\n' || *p == '\0')
            error_at(vm, start, "unclosed string literal");
        if (*p == '\\')
            p++;
    }
    return p;
}

static Token *read_string_literal(VirtualMachine *vm, char *start, char *quote) {
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
    Type *elem = copy_type(vm, ty_char);
    elem->is_const = true;
    tok->ty = array_of(vm, elem, len + 1);
    tok->str = buf;
    return tok;
}

// Read one raw fragment of a backtick quasi-quote. `token_start` includes the
// opening backtick for the first fragment and starts immediately after `}` for
// later fragments; `contents` always points at the first fragment byte.
// Only \` is interpreted, as an escaped literal backtick. Every other byte is
// copied verbatim for the underlying Quote template.
static Token *read_backtick_fragment(VirtualMachine *vm, char *token_start,
                                     char *contents, char **new_pos,
                                     bool *has_splice) {
    char *p = contents;
    char *buf = arena_alloc(&vm->compiler.parser_arena, strlen(contents) + 1);
    int len = 0;

    for (;;) {
        if (*p == '\0')
            error_at(vm, token_start, "unterminated backtick quasi-quote");

        if (p[0] == '\\' && p[1] == '`') {
            buf[len++] = '`';
            p += 2;
            continue;
        }

        if (p[0] == '$' && p[1] == '{') {
            Token *tok = new_token(vm, TK_BACKTICK_STR, token_start, p);
            buf[len] = '\0';
            tok->str = buf;
            *new_pos = p + 2;
            *has_splice = true;
            return tok;
        }

        if (*p == '`') {
            Token *tok = new_token(vm, TK_BACKTICK_STR, token_start, p + 1);
            buf[len] = '\0';
            tok->str = buf;
            *new_pos = p + 1;
            *has_splice = false;
            return tok;
        }

        buf[len++] = *p++;
    }
}

// Read a UTF-8-encoded string literal and transcode it in UTF-16.
//
// UTF-16 is yet another variable-width encoding for Unicode. Code
// points smaller than U+10000 are encoded in 2 bytes. Code points
// equal to or larger than that are encoded in 4 bytes. Each 2 bytes
// in the 4 byte sequence is called "surrogate", and a 4 byte sequence
// is called a "surrogate pair".
static Token *read_utf16_string_literal(VirtualMachine *vm, char *start, char *quote) {
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
static Token *read_utf32_string_literal(VirtualMachine *vm, char *start, char *quote, Type *ty) {
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

static Token *read_char_literal(VirtualMachine *vm, char *start, char *quote, Type *ty) {
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
    if (p != end)
        warn_tok(vm, tok, CCCC_WARN_MULTICHAR, "multi-character character constant");
    return tok;
}

// Compute the exact bit length (size of the minimal unsigned binary
// representation) of a nonnegative integer given as a digit string (no
// prefix, no suffix, no separators) in the given base. Returns 0 if the
// value is zero. Used to infer wb/uwb _BitInt width directly from the
// digit text, since materializing the value as an int64_t (as the rest of
// convert_pp_int does) truncates anything wider than 64 bits.
static int wide_digits_bit_length(const char *digits, int ndigits, int base) {
    int start = 0;
    while (start < ndigits && digits[start] == '0') start++;
    if (start == ndigits) return 0; // value is zero

    if (base == 2 || base == 8 || base == 16) {
        // Exact O(ndigits) shortcut: each digit maps to a fixed bit count.
        int bits_per_digit = (base == 2) ? 1 : (base == 8) ? 3 : 4;
        char c = digits[start];
        int digit_val = (c >= '0' && c <= '9') ? c - '0' :
                         (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
        int leading_bits = 32 - __builtin_clz((unsigned)digit_val);
        return leading_bits + (ndigits - start - 1) * bits_per_digit;
    }

    // base 10: no linear shortcut exists, so accumulate the full value at
    // compile time (in the compiler's own process, not the target VM) into
    // a word array sized to comfortably cover BITINT_MAXWIDTH, then find
    // the highest set bit.
    enum { MAX_WIDE_WORDS = 65535 / 64 + 4 };
    uint64_t words[MAX_WIDE_WORDS];
    memset(words, 0, sizeof(words));
    for (int i = start; i < ndigits; i++) {
        uint64_t carry = (uint64_t)(digits[i] - '0');
        for (int w = 0; w < MAX_WIDE_WORDS; w++) {
            __uint128_t prod = (__uint128_t)words[w] * 10 + carry;
            words[w] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
        }
    }
    for (int w = MAX_WIDE_WORDS - 1; w >= 0; w--) {
        if (words[w] != 0)
            return w * 64 + (64 - __builtin_clzll(words[w]));
    }
    return 0;
}

static bool convert_pp_int(VirtualMachine *vm, Token *tok) {
    char *p = tok->loc;

    // Read a binary, octal, decimal or hexadecimal number.
    int base = 10;
    if (!strncasecmp(p, "0x", 2) && isxdigit(p[2])) {
        p += 2;
        base = 16;
    } else if (!strncasecmp(p, "0b", 2) && (p[2] == '0' || p[2] == '1')) {
        if (vm->compiler.c_std < CCCC_STD_C23)
            warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                     "binary integer literals are a C23 extension");
        p += 2;
        base = 2;
    } else if (*p == '0') {
        base = 8;
    }

    // #776: a pp-number that is actually a floating constant (has a
    // decimal point, or an exponent letter appropriate to this base --
    // 'e'/'E' for octal/decimal, 'p'/'P' for hex; binary literals have no
    // floating-point form in C) must fall through to convert_pp_number's
    // strtold() path instead of being misparsed here. Checked over the
    // *whole* raw token before any digit-run collection below, so a case
    // like "08.5" (an invalid octal digit '8' followed by a decimal
    // point) is recognized as a float before the invalid-digit check
    // further down ever sees the '8'.
    //
    // This is the actual root cause of #776: the digit-collection loop
    // below treats 'e'/'E' as ordinary alnum digits (isalnum('e') is
    // true) and swallows a whole exponent like "1e10" into `cleaned`,
    // then strtoul() only consumes the leading "1" -- but the old code
    // trusted the *collected* length as bytes-consumed rather than
    // checking where strtoul() actually stopped, so "1e10" silently
    // "succeeded" as the integer 1 and never reached strtold().
    for (char *s = tok->loc; s < tok->loc + tok->len; s++) {
        if (*s == '.')
            return false;
        if ((base == 8 || base == 10) && (*s == 'e' || *s == 'E'))
            return false;
        if (base == 16 && (*s == 'p' || *s == 'P'))
            return false;
    }

    // C23: Remove digit separators (single quotes) before parsing
    // e.g., 1'000'000 becomes 1000000
    // Sized to tok->len+1 (an upper bound on digit count) rather than a
    // fixed 256-byte buffer, so very long literals (e.g. a 65535-bit binary
    // wb literal) aren't silently truncated.
    char *cleaned = arena_alloc(&vm->compiler.parser_arena, (size_t)tok->len + 1);
    int j = 0;
    for (char *s = p; *s; s++) {
        if (*s == '\'') {
            if (vm->compiler.c_std < CCCC_STD_C23)
                error_tok(vm, tok, "digit separators are not available before C23");
            continue;
        }
        // Stop at suffix (L, U, l, u) or non-alphanumeric
        if (!isalnum(*s))
            break;
        // Stop at suffix letters (L/U for existing suffixes; W for C23 wb/uwb)
        if ((*s == 'L' || *s == 'l' || *s == 'U' || *s == 'u' ||
             *s == 'W' || *s == 'w') && j > 0)
            break;
        cleaned[j++] = *s;
    }
    cleaned[j] = '\0';

    char *end_in_cleaned;
    int64_t val = strtoul(cleaned, &end_in_cleaned, base);

    // The float case (an 'e'/'E'/'p'/'P' exponent letter) was already
    // ruled out by the whole-token scan above, so if strtoul() didn't
    // consume every character the collection loop gathered, it's a
    // genuine invalid digit for this base (e.g. '8' in an octal
    // constant, '2' in a binary constant) -- a hard error rather than
    // silently truncating to whatever strtoul() managed to parse (#776).
    if (j > 0 && end_in_cleaned != cleaned + j) {
        const char *base_name = base == 16 ? "hexadecimal" :
                                 base == 8  ? "octal" :
                                 base == 2  ? "binary" : "integer";
        error_tok(vm, tok, "invalid digit '%c' in %s constant",
                  *end_in_cleaned, base_name);
    }

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

    // Read U, L or LL suffixes, plus C23 wb/uwb (_BitInt) suffixes.
    bool l = false;
    bool u = false;
    bool wb = false;

    // wb/uwb must be checked before the single-u branch to avoid misparse.
    if ((p[0] == 'u' || p[0] == 'U') &&
        (p[1] == 'w' || p[1] == 'W') &&
        (p[2] == 'b' || p[2] == 'B')) {
        p += 3;
        wb = true;
        u = true;
    } else if ((p[0] == 'w' || p[0] == 'W') && (p[1] == 'b' || p[1] == 'B')) {
        p += 2;
        wb = true;
    } else if (startswith(vm, p, "LLU") || startswith(vm, p, "LLu") ||
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

    // C23 wb/uwb: produce the smallest _BitInt(N) that can hold the value.
    // Width is derived from the full-precision digit text (`cleaned`), not
    // from `val` (which is truncated to 64 bits by strtoul above) — this is
    // what lets literals like 123456789012345678901234567890wb infer a
    // correct width beyond 64 bits instead of silently wrapping.
    if (wb) {
        int vbits = wide_digits_bit_length(cleaned, j, base);
        int width;
        if (u) {
            // unsigned _BitInt(N): minimum bits needed (at least 1)
            width = (vbits == 0) ? 1 : vbits;
        } else {
            // signed _BitInt(N): value bits + 1 sign bit (minimum 2)
            width = (vbits + 1 < 2) ? 2 : vbits + 1;
        }
        tok->kind = TK_NUM;
        tok->val = val;
        tok->ty = bitint_type(vm, tok, width, u);
        if (width > 64) {
            tok->wide_digits = cleaned;
            tok->wide_base = base;
        }
        return true;
    }

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
static void convert_pp_number(VirtualMachine *vm, Token *tok) {
    // Try to parse as an integer constant.
    if (convert_pp_int(vm, tok))
        return;

    // If it's not an integer, it must be a floating point constant.
    //
    // #782: strtold() has no knowledge of C23 digit separators (single
    // quotes) and simply stops at the first one, so a literal like
    // "1'000e5" would otherwise only parse "1" and leave the rest
    // unconsumed. Strip separators into a clean buffer first, the same way
    // convert_pp_int already does, and call strtold() on that instead.
    char *cleaned = arena_alloc(&vm->compiler.parser_arena, (size_t)tok->len + 1);
    int j = 0;
    for (char *s = tok->loc; s < tok->loc + tok->len; s++) {
        if (*s == '\'') {
            if (vm->compiler.c_std < CCCC_STD_C23)
                error_tok(vm, tok, "digit separators are not available before C23");
            continue;
        }
        cleaned[j++] = *s;
    }
    cleaned[j] = '\0';

    char *end;
    long double val = strtold(cleaned, &end);

    // C23 decimal-floating-constant suffixes df/DF/dd/DD/dl/DL (#402) --
    // exact-case pairs only, no mixed case, per the grammar. strtold() is
    // still used to locate where the numeric text ends (a plain decimal
    // digit sequence like "1" in "1dd" is valid strtold() syntax too), but
    // its *value* is discarded for a decimal literal: strtold() would
    // binary-round the value before BID ever saw the digit text, defeating
    // the entire point of a decimal type. tok->fval is left at 0.0 and
    // tok->dec_digits carries the verbatim (separator-stripped) digit text
    // instead, encoded to BID bits at codegen time (compile-time only,
    // requires CCCC_HAS_DECIMAL). A hex-float mantissa never takes a
    // decimal suffix -- decimal-floating-constant is a distinct grammar
    // production from hexadecimal-floating-constant -- so "0x1p3df" is
    // rejected below exactly like today's "0x1p3d" would be.
    bool is_hex = cleaned[0] == '0' && (cleaned[1] == 'x' || cleaned[1] == 'X');
    Type *dec_ty = NULL;
    if (!is_hex && end[0] && end[1]) {
        if ((end[0] == 'd' && end[1] == 'f') || (end[0] == 'D' && end[1] == 'F'))
            dec_ty = ty_decimal32;
        else if ((end[0] == 'd' && end[1] == 'd') || (end[0] == 'D' && end[1] == 'D'))
            dec_ty = ty_decimal64;
        else if ((end[0] == 'd' && end[1] == 'l') || (end[0] == 'D' && end[1] == 'L'))
            dec_ty = ty_decimal128;
    }

    if (dec_ty) {
        if (vm->compiler.c_std < CCCC_STD_C23)
            error_tok(vm, tok, "_Decimal literal suffixes are not available before C23");
        char *digits_end = end; // numeric text ends here, before the suffix
        end += 2;
        if (cleaned + j != end)
            error_tok(vm, tok, "invalid numeric constant");
        size_t digit_len = (size_t)(digits_end - cleaned);
        char *digits = arena_alloc(&vm->compiler.parser_arena, digit_len + 1);
        memcpy(digits, cleaned, digit_len);
        digits[digit_len] = '\0';
        tok->kind = TK_NUM;
        tok->fval = 0.0; // deliberately not strtold's value -- see above
        tok->dec_digits = digits;
        tok->ty = dec_ty;
        return;
    }

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

    if (cleaned + j != end)
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
static bool keyword_std_ok(VirtualMachine *vm, Token *t) {
    if (t->origin)
        return true;
    CStdVersion s = vm->compiler.c_std;
    int len = t->len;
    char *kw = t->loc;

#define KW(str) (len == (int)sizeof(str)-1 && memcmp(kw, str, len) == 0)

    // C99 reserved names — illegal below C99 (fire when --std=c89 is selected)
    if (s < CCCC_STD_C99) {
        if (KW("restrict") || KW("__restrict") || KW("__restrict__"))
            error_tok(vm, t, "'%.*s' is not available before C99", len, kw);
        if (KW("_Bool"))
            error_tok(vm, t, "'_Bool' is not available before C99");
        if (KW("_Complex") || KW("_Imaginary"))
            error_tok(vm, t, "'%.*s' is not available before C99", len, kw);
    }

    // C11 reserved names — illegal below C11
    if (s < CCCC_STD_C11) {
        if (KW("_Alignof") || KW("_Alignas"))
            error_tok(vm, t, "'%.*s' is not available before C11", len, kw);
        if (KW("_Noreturn"))
            warn_tok(vm, t, CCCC_WARN_PEDANTIC, "'_Noreturn' is not available before C11");
        if (KW("_Thread_local") || KW("__thread"))
            error_tok(vm, t, "'%.*s' is not available before C11", len, kw);
        if (KW("_Atomic"))
            error_tok(vm, t, "'_Atomic' is not available before C11");
        if (KW("_Static_assert"))
            error_tok(vm, t, "'_Static_assert' is not available before C11");
    }

    // C23 keywords that were valid identifiers in earlier standards — downgrade
    if (s < CCCC_STD_C23) {
        if (KW("constexpr") || KW("static_assert"))
            return false;
        if (KW("bool") || KW("true") || KW("false") || KW("nullptr") ||
            KW("thread_local"))
            return false;
        if (KW("_BitInt") || KW("_Decimal32") || KW("_Decimal64") || KW("_Decimal128"))
            error_tok(vm, t, "'%.*s' is not available before C23", len, kw);
    }

#undef KW
    return true;
}

void convert_pp_tokens(VirtualMachine *vm, Token *tok) {
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
static void add_line_numbers(VirtualMachine *vm, Token *tok) {
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

Token *tokenize_string_literal(VirtualMachine *vm, Token *tok, Type *basety) {
    Token *t;
    if (basety->size == 2)
        t = read_utf16_string_literal(vm, tok->loc, tok->loc);
    else
        t = read_utf32_string_literal(vm, tok->loc, tok->loc, basety);
    t->next = tok->next;
    return t;
}

// Tokenize a given string and returns new tokens.
Token *tokenize(VirtualMachine *vm, File *file) {
    vm->compiler.current_file = file;

    char *p = file->contents;
    Token head = {};
    Token *cur = &head;

    vm->compiler.at_bol = true;
    vm->compiler.has_space = false;

    // State tracking for #include directive to preserve // in URLs
    bool after_include_directive = false;  // True after we see #include
    bool in_include_path = false;          // True when inside <...> or "..." of #include
    bool in_backtick_splice = false;
    int backtick_brace_depth = 0;
    char *backtick_splice_start = NULL;

    while (*p) {
        // Skip line comments (but NOT inside #include paths where URLs may contain //)
        if (startswith(vm, p, "//") && !in_include_path) {
            if (vm->compiler.c_std < CCCC_STD_C99)
                warn_at(vm, p, CCCC_WARN_PEDANTIC,
                        "'//' comments are a C99 extension");
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

        if (in_backtick_splice && *p == '`')
            error_at(vm, p, "nested backtick quasi-quotes are not supported");

        // Backtick quasi-quote. The raw fragment is one token; `${` enters
        // normal C tokenization so preprocessing and parsing apply to the
        // interpolation expression exactly as they do elsewhere.
        if (!in_backtick_splice && *p == '`') {
            char *start = p++;
            bool has_splice = false;
            cur = cur->next = read_backtick_fragment(vm, start, p, &p,
                                                      &has_splice);
            if (has_splice) {
                cur = cur->next = new_token(vm, TK_PUNCT, p - 2, p - 1);
                cur = cur->next = new_token(vm, TK_PUNCT, p - 1, p);
                in_backtick_splice = true;
                backtick_brace_depth = 0;
                backtick_splice_start = p - 2;
            }
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

        // UTF-8 character literal (C23 u8'x')
        if (startswith(vm, p, "u8'")) {
            if (vm->compiler.c_std < CCCC_STD_C23)
                warn_tok(vm, new_token(vm, TK_PUNCT, p, p + 3), CCCC_WARN_PEDANTIC,
                         "u8 character literals are a C23 extension");
            cur = cur->next = read_char_literal(vm, p, p + 2, ty_uchar);
            cur->val &= 0xff;
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
            if (in_backtick_splice && punct_len == 1 && *p == '{') {
                backtick_brace_depth++;
            } else if (in_backtick_splice && punct_len == 1 && *p == '}') {
                if (backtick_brace_depth > 0) {
                    backtick_brace_depth--;
                } else {
                    cur = cur->next = new_token(vm, TK_PUNCT, p, p + 1);
                    p++;

                    bool has_splice = false;
                    cur = cur->next = read_backtick_fragment(vm, p, p, &p,
                                                              &has_splice);
                    if (has_splice) {
                        cur = cur->next = new_token(vm, TK_PUNCT, p - 2, p - 1);
                        cur = cur->next = new_token(vm, TK_PUNCT, p - 1, p);
                        backtick_splice_start = p - 2;
                    } else {
                        in_backtick_splice = false;
                        backtick_splice_start = NULL;
                    }
                    continue;
                }
            }
            cur = cur->next = new_token(vm, TK_PUNCT, p, p + punct_len);
            p += cur->len;
            continue;
        }

        error_at(vm, p, "invalid token");
    }

    if (in_backtick_splice)
        error_at(vm, backtick_splice_start,
                 "unterminated backtick interpolation; expected '}'");

    cur = cur->next = new_token(vm, TK_EOF, p, p);
    add_line_numbers(vm, head.next);
    return head.next;
}

// Returns the contents of a given file.
static char *read_file(VirtualMachine *vm, char *path) {
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
unsigned char *read_binary_file(VirtualMachine *vm, char *path, size_t *out_size) {
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

File *new_file(VirtualMachine *vm, char *name, int file_no, char *contents) {
    File *file = arena_alloc(&vm->compiler.parser_arena, sizeof(File));
    memset(file, 0, sizeof(File));
    file->name = name;
    file->display_name = name;
    file->file_no = file_no;
    file->contents = contents;
    return file;
}

// A leading "#!" line would otherwise be parsed as a preprocessor directive.
// Overwrite it with spaces rather than deleting it so line and column numbers
// in diagnostics still match the physical file.
static void blank_shebang_line(char *p) {
    if (p[0] != '#' || p[1] != '!')
        return;
    for (; *p && *p != '\n'; p++)
        *p = ' ';
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
static void convert_universal_chars(VirtualMachine *vm, char *p) {
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

Token *tokenize_file(VirtualMachine *vm, char *path, bool allow_shebang) {
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
    if (allow_shebang)
        blank_shebang_line(p);
    remove_backslash_newline(p);
    convert_universal_chars(vm, p);

    // Save the filename for assembler .file directive.
    File *file = new_file(vm, path, vm->compiler.file_no + 1, p);

    // Save the filename for assembler .file directive.
    vm->compiler.input_files = realloc(vm->compiler.input_files, sizeof(char *) * (vm->compiler.file_no + 2));
    vm->compiler.input_files[vm->compiler.file_no] = file;
    vm->compiler.input_files[vm->compiler.file_no + 1] = NULL;
    vm->compiler.file_no++;

    return tokenize(vm, file);
}

// Tokenize an in-memory string (for embedded headers)
Token *tokenize_string(VirtualMachine *vm, char *name, char *contents) {
    // Duplicate contents because tokenize may modify it
    char *p = arena_alloc(&vm->compiler.parser_arena, strlen(contents) + 1);
    strcpy(p, contents);

    canonicalize_newline(p);
    remove_backslash_newline(p);
    convert_universal_chars(vm, p);

    File *file = new_file(vm, name, -(vm->compiler.embedded_file_no + 1), p);
    vm->compiler.embedded_file_no++;

    return tokenize(vm, file);
}

static bool token_is_c23_attr_start(Token *tok) {
    return tok && equal(tok, "[") && tok->next && equal(tok->next, "[");
}

static bool token_is_gnu_attr_start(Token *tok) {
    return tok && equal(tok, "__attribute__") && tok->next &&
           equal(tok->next, "(") && tok->next->next &&
           equal(tok->next->next, "(");
}

static bool attr_name_is_cccc(char *name) {
    return !strcmp(name, "comptime") || !strcmp(name, "emit") ||
           !strcmp(name, "macro") || !strcmp(name, "test") ||
           !strcmp(name, "test_setup") || !strcmp(name, "test_teardown");
}

static bool attr_name_is_std(char *name) {
    return !strcmp(name, "maybe_unused") || !strcmp(name, "deprecated") ||
           !strcmp(name, "noreturn") || !strcmp(name, "nodiscard") ||
           !strcmp(name, "fallthrough") || !strcmp(name, "no_unique_address");
}

static void copy_attr_name(Token *tok, char *buf, size_t cap) {
    if (!tok || cap == 0) {
        return;
    }
    size_t len = tok->len < (int)cap - 1 ? (size_t)tok->len : cap - 1;
    memcpy(buf, tok->loc, len);
    buf[len] = '\0';
}

static CCCCAttrTarget effective_attr_target(VirtualMachine *vm, char *name) {
    CCCCAttrTarget target = vm ? vm->compiler.attr_target : CCCC_ATTR_TARGET_AUTO;
    if (target != CCCC_ATTR_TARGET_AUTO)
        return target;
    if (vm && vm->compiler.c_std >= CCCC_STD_C23 && attr_name_is_std(name))
        return CCCC_ATTR_TARGET_C23;
    return CCCC_ATTR_TARGET_GNU;
}

static Token *find_c23_attr_close(Token *tok) {
    if (!token_is_c23_attr_start(tok))
        return NULL;
    int depth = 0;
    for (Token *p = tok->next->next; p && p->kind != TK_EOF; p = p->next) {
        if (equal(p, "["))
            depth++;
        else if (equal(p, "]")) {
            if (depth == 0 && p->next && equal(p->next, "]"))
                return p;
            depth--;
        }
    }
    return NULL;
}

static Token *find_gnu_attr_close(Token *tok) {
    if (!token_is_gnu_attr_start(tok))
        return NULL;
    int depth = 0;
    for (Token *p = tok->next->next->next; p && p->kind != TK_EOF; p = p->next) {
        if (equal(p, "("))
            depth++;
        else if (equal(p, ")")) {
            if (depth == 0 && p->next && equal(p->next, ")"))
                return p;
            depth--;
        }
    }
    return NULL;
}

static void print_attr_payload(FILE *f, Token *start, Token *end) {
    bool need_space = false;
    for (Token *p = start; p && p != end; p = p->next) {
        bool punct = p->len == 1 && strchr("()[]{}.,:;", *p->loc);
        if (need_space && !punct && !(p->len == 1 && *p->loc == ')'))
            fputc(' ', f);
        fprintf(f, "%.*s", p->len, p->loc);
        need_space = !(p->len == 1 && strchr("([{:.", *p->loc));
    }
}

static bool output_attr(FILE *f, VirtualMachine *vm, Token **tok_ptr) {
    Token *tok = *tok_ptr;
    bool c23 = token_is_c23_attr_start(tok);
    bool gnu = token_is_gnu_attr_start(tok);
    if (!c23 && !gnu)
        return false;

    Token *payload = c23 ? tok->next->next : tok->next->next->next;
    Token *close = c23 ? find_c23_attr_close(tok) : find_gnu_attr_close(tok);
    if (!close)
        return false;
    Token *end = c23 ? close->next->next : close->next->next;

    Token *name_tok = payload;
    bool cccc_scoped = false;
    if (c23 && equal(name_tok, "cccc") && name_tok->next &&
        equal(name_tok->next, ":") && name_tok->next->next &&
        equal(name_tok->next->next, ":") && name_tok->next->next->next) {
        cccc_scoped = true;
        name_tok = name_tok->next->next->next;
    }

    char name[128] = "";
    copy_attr_name(name_tok, name, sizeof(name));
    if (vm && vm->compiler.emit_cccc && (cccc_scoped || attr_name_is_cccc(name))) {
        // --emit-cccc: preserve CCCC-scoped attrs verbatim as
        // [[cccc::name(...)]] rather than stripping them. `payload` already
        // starts at "cccc" for the explicitly-scoped form; a bare cccc-only
        // name (e.g. [[comptime]]) needs the "cccc::" prefix added back.
        fprintf(f, "[[");
        if (!cccc_scoped)
            fprintf(f, "cccc::");
        print_attr_payload(f, payload, close);
        fprintf(f, "]]");
        *tok_ptr = end;
        return true;
    }
    if (cccc_scoped || attr_name_is_cccc(name) ||
        (vm && vm->compiler.attr_target == CCCC_ATTR_TARGET_STRIP)) {
        *tok_ptr = end;
        return true;
    }

    CCCCAttrTarget target = effective_attr_target(vm, name);
    switch (target) {
    case CCCC_ATTR_TARGET_C23:
        fprintf(f, "[[");
        print_attr_payload(f, payload, close);
        fprintf(f, "]]");
        break;
    case CCCC_ATTR_TARGET_MSVC:
        fprintf(f, "__declspec(");
        print_attr_payload(f, payload, close);
        fprintf(f, ")");
        break;
    case CCCC_ATTR_TARGET_GNU:
    case CCCC_ATTR_TARGET_AUTO:
    default:
        fprintf(f, "__attribute__((");
        print_attr_payload(f, payload, close);
        fprintf(f, "))");
        break;
    }

    *tok_ptr = end;
    return true;
}

// Output preprocessed tokens as source code (for -E flag)
void cc_output_preprocessed(FILE *f, VirtualMachine *vm, Token *tok) {
    if (!f || !tok)
        return;

    // Re-emit any libraries queued via #pragma cccc link() or
    // #pragma comment(lib, ...) as the portable comment(lib, ...) form.
    // The pragmas are consumed during preprocessing so they don't appear
    // in the token stream; emit them here before the token output.
    if (vm) {
        for (int i = 0; i < vm->compiler.pragma_link_libs.len; i++)
            fprintf(f, "#pragma comment(lib, \"%s\")\n",
                    vm->compiler.pragma_link_libs.data[i]);
        if (vm->compiler.pragma_link_libs.len > 0)
            fprintf(f, "\n");
    }

    int at_bol = 1;

    for (Token *t = tok; t && t->kind != TK_EOF;) {
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

        if (output_attr(f, vm, &t)) {
            at_bol = 0;
            continue;
        }

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
        t = t->next;
    }

    fprintf(f, "\n");
}

// Error collection helper functions

int cc_get_error_count(VirtualMachine *vm) {
    return vm ? vm->error_count : 0;
}

int cc_get_warning_count(VirtualMachine *vm) {
    return vm ? vm->warning_count : 0;
}

bool cc_has_errors(VirtualMachine *vm) {
    return vm && vm->error_count > 0;
}

void cc_clear_errors(VirtualMachine *vm) {
    if (!vm) return;

    vm->errors = NULL;
    vm->errors_tail = NULL;
    vm->error_count = 0;
    vm->warning_count = 0;
    vm->error_message = NULL;
}

void cc_print_all_errors(VirtualMachine *vm) {
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

    // Print summary (suppressed in JSON mode)
    if (!vm->compiler.diagnostic_json && (error_num > 0 || warning_num > 0)) {
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
