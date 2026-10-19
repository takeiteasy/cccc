// testcheck -- run one loose test file against a cccc binary.
//
//   testcheck [--timeout SECS] [--native] <cccc> <test.c>
//   testcheck --lint <test.c>...        compile every pattern, run nothing
//
// --native builds the test with -c=native and runs the binary (a --testing
// test runs as --testing=native). It skips tests that expect a compile error,
// carry CCCC_NATIVE_SKIP, or pick their own output mode (-c, -m, -o, -E,
// --build*, --watch, --emit-cccc).
//
// The test's leading comment block carries the directives:
//   CCCC_FLAGS: <flags>            extra cccc arguments (whitespace split)
//   CCCC_RUN_ARGS: <args>          arguments for the test program (after --)
//   CCCC_EXPECT_STDOUT: <regex>    stdout must match   (repeatable, all apply)
//   CCCC_REJECT_STDOUT: <regex>    stdout must not match
//   CCCC_EXPECT_STDERR: <regex>    stdout+stderr must match
//   CCCC_REJECT_STDERR: <regex>    stdout+stderr must not match
//   EXPECT_COMPILE_ERROR           cccc fails with a diagnostic
//   EXPECT_RUNTIME_ERROR           the program exits 255
//   CCCC_NATIVE_SKIP[: reason]     left out of the --native pass
// Otherwise the program must exit 42 (see pass_rule()).
//
// EXPECT patterns for TAP `ok N` lines are written `^ok N`; an unanchored `ok`
// also matches inside `not ok`. A --testing run additionally fails on any
// `not ok` / `✗` / JSON fail or timeout line that no EXPECT pattern matches.
//
// Patterns are POSIX ERE with a few Python-style escapes translated
// (translate_pattern()). STDOUT patterns see `.` match newlines and `^`/`$`
// anchor at line boundaries; STDERR patterns use REG_NEWLINE.

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_PATTERNS 32
#define MAX_ARGS     64
#define OUTPUT_CAP   (64u << 20)

typedef struct {
    char *flags[MAX_ARGS];
    int   nflags;
    char *run_args[MAX_ARGS];
    int   nrun_args;
    char *expect_out[MAX_PATTERNS], *reject_out[MAX_PATTERNS];
    char *expect_err[MAX_PATTERNS], *reject_err[MAX_PATTERNS];
    int   nexpect_out, nreject_out, nexpect_err, nreject_err;
    int   compile_error, runtime_error;
    int   testing, build, drives_output;
    char *native_skip;
} Header;

typedef struct {
    char  *data;
    size_t len, cap;
} Buf;

static void die(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(2);
}

static void split_words(char **out, int *n, char *s) {
    for (char *tok = strtok(s, " \t"); tok; tok = strtok(NULL, " \t")) {
        if (*n >= MAX_ARGS)
            die("too many arguments in header");
        out[(*n)++] = strdup(tok);
    }
}

static void add_pattern(char **list, int *n, const char *v, const char *file,
                        int must_match) {
    if (*n >= MAX_PATTERNS)
        die("%s: too many patterns", file);
    // `ok 2 - x` also matches inside `not ok 2 - x`.
    if (must_match && !strncmp(v, "ok ", 3))
        die("%s: pattern '%s' must be anchored as '^%s'", file, v, v);
    list[(*n)++] = strdup(v);
}

static int is_name_char(int c) {
    return isupper(c) || isdigit(c) || c == '_';
}

// Applies one anchored directive at *p; returns the rest of the line to scan
// for a chained bare directive, or NULL when the line is consumed.
static char *apply_directive(Header *h, char *p, const char *file) {
    static const struct {
        const char *name;
        int         id;
    } value_dirs[] = {
        {"CCCC_FLAGS", 0},         {"CCCC_RUN_ARGS", 1},
        {"CCCC_EXPECT_STDOUT", 2}, {"CCCC_REJECT_STDOUT", 3},
        {"CCCC_EXPECT_STDERR", 4}, {"CCCC_REJECT_STDERR", 5},
    };
    char *e = p;
    while (is_name_char((unsigned char)*e))
        e++;
    size_t len = (size_t)(e - p);
    if (!len)
        return NULL;
    char name[64];
    if (len >= sizeof(name))
        return NULL;
    memcpy(name, p, len);
    name[len] = '\0';

    if (*e == ':') {
        char *v = e + 1;
        while (*v == ' ' || *v == '\t')
            v++;
        size_t vl = strlen(v);
        while (vl && isspace((unsigned char)v[vl - 1]))
            vl--;
        if (vl >= 2 && v[vl - 2] == '*' && v[vl - 1] == '/') {
            vl -= 2;
            while (vl && isspace((unsigned char)v[vl - 1]))
                vl--;
        }
        v[vl] = '\0';
        for (size_t i = 0; i < sizeof(value_dirs) / sizeof(*value_dirs); i++) {
            if (strcmp(name, value_dirs[i].name))
                continue;
            switch (value_dirs[i].id) {
                case 0:
                    split_words(h->flags, &h->nflags, v);
                    break;
                case 1:
                    split_words(h->run_args, &h->nrun_args, v);
                    break;
                case 2:
                    add_pattern(h->expect_out, &h->nexpect_out, v, file, 1);
                    break;
                case 3:
                    add_pattern(h->reject_out, &h->nreject_out, v, file, 0);
                    break;
                case 4:
                    add_pattern(h->expect_err, &h->nexpect_err, v, file, 1);
                    break;
                case 5:
                    add_pattern(h->reject_err, &h->nreject_err, v, file, 0);
                    break;
            }
            return NULL;
        }
        if (!strcmp(name, "CCCC_NATIVE_SKIP")) {
            h->native_skip = strdup(*v ? v : "CCCC_NATIVE_SKIP");
            return NULL;
        }
        if (!strcmp(name, "EXPECT_COMPILE_ERROR")) {
            h->compile_error = 1;
            return NULL;
        }
        if (!strcmp(name, "EXPECT_RUNTIME_ERROR")) {
            h->runtime_error = 1;
            return NULL;
        }
        if (!strncmp(name, "CCCC_", 5))
            die("%s: unknown directive %s", file, name);
        return NULL;
    }
    if (*e && !isspace((unsigned char)*e))
        return NULL;
    if (!strcmp(name, "EXPECT_COMPILE_ERROR")) {
        h->compile_error = 1;
        return e;
    }
    if (!strcmp(name, "EXPECT_RUNTIME_ERROR")) {
        h->runtime_error = 1;
        return e;
    }
    return NULL;
}

static int drives_output(const char *f) {
    static const char *const exact[] = {
        "-c", "-m", "-o", "-E", "--dump-expanded", "--watch", "--emit-cccc"};
    static const char *const prefix[] = {"-c=", "--build"};
    for (size_t i = 0; i < sizeof(exact) / sizeof(*exact); i++)
        if (!strcmp(f, exact[i]))
            return 1;
    for (size_t i = 0; i < sizeof(prefix) / sizeof(*prefix); i++)
        if (!strncmp(f, prefix[i], strlen(prefix[i])))
            return 1;
    return 0;
}

static void parse_header(Header *h, const char *file) {
    FILE *f = fopen(file, "r");
    if (!f)
        die("%s: %s", file, strerror(errno));
    char line[8192];
    for (int n = 0; n < 200 && fgets(line, sizeof(line), f); n++) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\n' || *p == '\0')
            continue;
        if (!strncmp(p, "//", 2) || !strncmp(p, "/*", 2))
            p += 2;
        else if (*p == '*' && p[1] != '/')
            p += 1;
        else
            break;
        for (;;) {
            while (*p == ' ' || *p == '\t')
                p++;
            p[strcspn(p, "\r\n")] = '\0';
            char *rest            = apply_directive(h, p, file);
            if (!rest)
                break;
            p = rest;
        }
    }
    fclose(f);
    for (int i = 0; i < h->nflags; i++) {
        if (!strcmp(h->flags[i], "--testing") ||
            !strncmp(h->flags[i], "--testing=", 10))
            h->testing = 1;
        if (!strcmp(h->flags[i], "--build"))
            h->build = 1;
        if (drives_output(h->flags[i]))
            h->drives_output = 1;
    }
}

// ---- pattern translation ---------------------------------------------------

typedef struct {
    char  *s;
    size_t len, cap;
} Str;

static void str_add(Str *o, const char *t) {
    size_t n = strlen(t);
    if (o->len + n + 1 > o->cap) {
        o->cap = (o->len + n + 1) * 2;
        o->s   = realloc(o->s, o->cap);
    }
    memcpy(o->s + o->len, t, n + 1);
    o->len += n;
}

static void str_addc(Str *o, char c) {
    char t[2] = {c, 0};
    str_add(o, t);
}

// Translates Python-style escapes into POSIX ERE. Returns NULL and sets *err
// for anything it cannot express (lookahead, \b, ...), so such patterns get
// rewritten in the test rather than silently changing meaning.
//   outside [...]: \s \S \d \D \w \W \n \t \A \Z, (?:, lazy quantifiers,
//                  and, when `multiline`, ^/$ matching at line boundaries
//   inside  [...]: \s \d \w \n \t and backslash-escaped punctuation
//   [\s\S]       : any character including newline
static char *translate_pattern(const char *p, int multiline, const char **err) {
    Str o = {0};
    str_add(&o, "");
    for (size_t i = 0; p[i];) {
        char c = p[i];
        if (c == '[') {
            if (!strncmp(p + i, "[\\s\\S]", 6) ||
                !strncmp(p + i, "[\\S\\s]", 6)) {
                str_add(&o, "(.|\n)");
                i += 6;
                continue;
            }
            str_addc(&o, '[');
            i++;
            if (p[i] == '^')
                str_addc(&o, p[i++]);
            if (p[i] == ']')
                str_addc(&o, p[i++]);
            while (p[i] && p[i] != ']') {
                if (p[i] == '[' && p[i + 1] == ':') {
                    const char *end = strstr(p + i, ":]");
                    if (!end) {
                        *err = "unterminated [:class:]";
                        goto fail;
                    }
                    str_add(&o, "[:");
                    for (const char *q = p + i + 2; q < end; q++)
                        str_addc(&o, *q);
                    str_add(&o, ":]");
                    i = (size_t)(end - p) + 2;
                    continue;
                }
                if (p[i] != '\\') {
                    str_addc(&o, p[i++]);
                    continue;
                }
                char e  = p[i + 1];
                i      += 2;
                switch (e) {
                    case 's':
                        str_add(&o, "[:space:]");
                        break;
                    case 'd':
                        str_add(&o, "[:digit:]");
                        break;
                    case 'w':
                        str_add(&o, "[:alnum:]_");
                        break;
                    case 'n':
                        str_addc(&o, '\n');
                        break;
                    case 't':
                        str_addc(&o, '\t');
                        break;
                    case '\0':
                        *err = "trailing backslash";
                        goto fail;
                    case ']':
                    case '-':
                    case '^':
                        *err = "escaped ]/-/^ inside a bracket expression";
                        goto fail;
                    default:
                        if (isalnum((unsigned char)e)) {
                            *err = "unsupported escape inside a bracket "
                                   "expression";
                            goto fail;
                        }
                        str_addc(&o, e);
                }
            }
            if (p[i] != ']') {
                *err = "unterminated bracket expression";
                goto fail;
            }
            str_addc(&o, p[i++]);
            continue;
        }
        if (c == '\\') {
            char e  = p[i + 1];
            i      += 2;
            switch (e) {
                case 's':
                    str_add(&o, "[[:space:]]");
                    break;
                case 'S':
                    str_add(&o, "[^[:space:]]");
                    break;
                case 'd':
                    str_add(&o, "[0-9]");
                    break;
                case 'D':
                    str_add(&o, "[^0-9]");
                    break;
                case 'w':
                    str_add(&o, "[[:alnum:]_]");
                    break;
                case 'W':
                    str_add(&o, "[^[:alnum:]_]");
                    break;
                case 'n':
                    str_addc(&o, '\n');
                    break;
                case 't':
                    str_addc(&o, '\t');
                    break;
                case 'A':
                    str_addc(&o, '^');
                    break;
                case 'Z':
                    str_addc(&o, '$');
                    break;
                case '\0':
                    *err = "trailing backslash";
                    goto fail;
                default:
                    if (isalnum((unsigned char)e)) {
                        *err = "unsupported escape";
                        goto fail;
                    }
                    str_addc(&o, '\\');
                    str_addc(&o, e);
            }
            continue;
        }
        if (c == '(' && p[i + 1] == '?') {
            if (p[i + 2] == ':') {
                str_addc(&o, '(');
                i += 3;
                continue;
            }
            *err = "(?...) group other than (?:";
            goto fail;
        }
        if ((c == '*' || c == '+' || c == '?') && p[i + 1] == '?') {
            str_addc(&o, c);
            i += 2;
            continue;
        }
        if (multiline && c == '^') {
            str_add(&o, "(^|\n)");
            i++;
            continue;
        }
        if (multiline && c == '$') {
            str_add(&o, "($|\n)");
            i++;
            continue;
        }
        str_addc(&o, c);
        i++;
    }
    return o.s;
fail:
    free(o.s);
    return NULL;
}

static int compile_pattern(regex_t *re, const char *pat, int is_stdout,
                           const char *file, int report) {
    const char *err = NULL;
    char       *t   = translate_pattern(pat, is_stdout, &err);
    if (!t) {
        if (report)
            fprintf(stderr, "%s: pattern /%s/: %s\n", file, pat, err);
        return -1;
    }
    int rc = regcomp(re, t, REG_EXTENDED | (is_stdout ? 0 : REG_NEWLINE));
    if (rc) {
        char msg[256];
        regerror(rc, re, msg, sizeof(msg));
        if (report)
            fprintf(stderr, "%s: pattern /%s/ -> /%s/: %s\n", file, pat, t,
                    msg);
        free(t);
        return -1;
    }
    free(t);
    return 0;
}

// ---- running ---------------------------------------------------------------

static void buf_add(Buf *b, const char *d, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap  = (b->len + n + 1) * 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, d, n);
    b->len          += n;
    b->data[b->len]  = '\0';
}

// Returns the wait status, or -1 on timeout (child group killed).
static int run_capture(char *const argv[], Buf *out, Buf *err, int timeout) {
    int po[2], pe[2];
    if (pipe(po) || pipe(pe))
        die("pipe: %s", strerror(errno));
    pid_t pid = fork();
    if (pid < 0)
        die("fork: %s", strerror(errno));
    if (pid == 0) {
        setpgid(0, 0);
        int nul = open("/dev/null", O_RDONLY);
        dup2(nul, 0);
        dup2(po[1], 1);
        dup2(pe[1], 2);
        close(po[0]);
        close(po[1]);
        close(pe[0]);
        close(pe[1]);
        execv(argv[0], argv);
        fprintf(stderr, "exec %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    close(po[1]);
    close(pe[1]);
    struct pollfd fds[2]   = {{po[0], POLLIN, 0}, {pe[0], POLLIN, 0}};
    Buf          *bufs[2]  = {out, err};
    time_t        deadline = time(NULL) + timeout;
    int           open_fds = 2, timed_out = 0;
    char          chunk[65536];
    while (open_fds > 0) {
        long left = (long)(deadline - time(NULL));
        if (left <= 0) {
            timed_out = 1;
            break;
        }
        int pr = poll(fds, 2, (int)(left > 1000 ? 1000 : left) * 1000);
        if (pr < 0 && errno != EINTR)
            break;
        for (int i = 0; i < 2; i++) {
            if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP)))
                continue;
            ssize_t n = read(fds[i].fd, chunk, sizeof(chunk));
            if (n > 0) {
                if (bufs[i]->len < OUTPUT_CAP)
                    buf_add(bufs[i], chunk, (size_t)n);
            } else if (n == 0 || errno != EINTR) {
                close(fds[i].fd);
                fds[i].fd = -1;
                open_fds--;
            }
        }
    }
    if (timed_out)
        kill(-pid, SIGKILL);
    for (int i = 0; i < 2; i++)
        if (fds[i].fd >= 0)
            close(fds[i].fd);
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    kill(-pid, SIGKILL);
    return timed_out ? -1 : status;
}

// A failed compile prints "error"/"errors generated" or one of the
// diagnostics below.
static int has_diagnostic(const char *out) {
    static const char *const marks[] = {"error", "undefined function",
                                        "undefined global", "cannot open file",
                                        "unknown warning option"};
    for (size_t i = 0; i < sizeof(marks) / sizeof(*marks); i++)
        if (strstr(out, marks[i]))
            return 1;
    // comptime diagnostics: "expected unique generated name, got existing ..."
    return strstr(out, "expected") && strstr(out, " got ");
}

static void print_tail(const char *label, const Buf *b) {
    if (!b->len)
        return;
    const char *p     = b->data + b->len;
    int         lines = 0;
    while (p > b->data && lines < 25)
        if (*--p == '\n')
            lines++;
    if (*p == '\n' && p < b->data + b->len - 1)
        p++;
    fprintf(stderr, "--- %s (tail) ---\n%s%s", label, p,
            b->data[b->len - 1] == '\n' ? "" : "\n");
}

// Exit statuses that pass when no EXPECT_COMPILE_ERROR / EXPECT_RUNTIME_ERROR.
// 42 always; 0 for --testing suites and tests that check stdout; and any
// non-zero status for --testing / --build tests that EXPECT_STDERR a failure.
static int pass_rule(const Header *h, int code) {
    if (code == 42)
        return 1;
    if (code == 0 && (h->testing || h->nexpect_out))
        return 1;
    if (code != 0 && (h->testing || h->build) && h->nexpect_err)
        return 1;
    return 0;
}

static int check_patterns(const Header *h, const Buf *out, const Buf *err,
                          const char *file) {
    Buf both = {0};
    buf_add(&both, out->data ? out->data : "", out->len);
    buf_add(&both, err->data ? err->data : "", err->len);
    struct {
        char *const *list;
        int          n, is_stdout, want;
        const char  *what;
    } checks[] = {
        {h->expect_out, h->nexpect_out, 1, 1, "stdout to match"},
        {h->reject_out, h->nreject_out, 1, 0, "stdout not to match"},
        {h->expect_err, h->nexpect_err, 0, 1, "stderr to match"},
        {h->reject_err, h->nreject_err, 0, 0, "stderr not to match"},
    };
    int ok = 1;
    for (int c = 0; c < 4 && ok; c++) {
        for (int i = 0; i < checks[c].n && ok; i++) {
            regex_t re;
            if (compile_pattern(&re, checks[c].list[i], checks[c].is_stdout,
                                file, 1))
                die("%s: bad pattern", file);
            const char *hay =
                checks[c].is_stdout ? (out->data ? out->data : "") : both.data;
            int matched = regexec(&re, hay, 0, NULL, 0) == 0;
            regfree(&re);
            if (matched != checks[c].want) {
                fprintf(stderr, "FAIL %s: expected %s: %s\n", file,
                        checks[c].what, checks[c].list[i]);
                ok = 0;
            }
        }
    }
    free(both.data);
    return ok;
}

static int is_failure_line(const char *line) {
    while (*line == ' ' || *line == '\t')
        line++;
    return !strncmp(line, "not ok ", 7) || !strncmp(line, "\xE2\x9C\x97", 3) ||
           strstr(line, "\"status\":\"fail\"") ||
           strstr(line, "\"status\":\"timeout\"");
}

static int line_matches_expect(const Header *h, const char *line,
                               const char *file) {
    struct {
        char *const *list;
        int          n, is_stdout;
    } all[] = {
        {h->expect_out, h->nexpect_out, 1},
        {h->expect_err, h->nexpect_err, 0},
    };
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < all[c].n; i++) {
            regex_t re;
            if (compile_pattern(&re, all[c].list[i], all[c].is_stdout, file, 1))
                die("%s: bad pattern", file);
            int matched = regexec(&re, line, 0, NULL, 0) == 0;
            regfree(&re);
            if (matched)
                return 1;
        }
    return 0;
}

// A --testing run reports every failed or timed-out test; each such line must
// be one the test expects, so a regression cannot hide behind a pattern that
// matches only the passing lines.
static int check_failure_lines(const Header *h, const Buf *out, const Buf *err,
                               const char *file) {
    const Buf *streams[] = {out, err};
    for (int s = 0; s < 2; s++) {
        const char *p = streams[s]->data;
        while (p && *p) {
            size_t n    = strcspn(p, "\n");
            char  *line = strndup(p, n);
            int    bad  = is_failure_line(line) &&
                       !line_matches_expect(h, line, file);
            if (bad)
                fprintf(stderr, "FAIL %s: unexpected failure line: %s\n", file,
                        line);
            free(line);
            if (bad)
                return 0;
            p += n + (p[n] == '\n');
        }
    }
    return 1;
}

static int lint(char **files, int n) {
    int bad = 0;
    for (int i = 0; i < n; i++) {
        Header h = {0};
        parse_header(&h, files[i]);
        regex_t re;
        struct {
            char **list;
            int    n, is_stdout;
        } all[] = {
            {h.expect_out, h.nexpect_out, 1},
            {h.reject_out, h.nreject_out, 1},
            {h.expect_err, h.nexpect_err, 0},
            {h.reject_err, h.nreject_err, 0},
        };
        for (int c = 0; c < 4; c++)
            for (int j = 0; j < all[c].n; j++) {
                if (compile_pattern(&re, all[c].list[j], all[c].is_stdout,
                                    files[i], 1))
                    bad++;
                else
                    regfree(&re);
            }
    }
    return bad ? 1 : 0;
}

int main(int argc, char **argv) {
    int timeout = 600, native = 0, a = 1;
    setenv("CCCC_NO_CONFIG", "1", 1); // a developer's ~/.cccc.toml must not leak in
    if (a < argc && !strcmp(argv[a], "--lint"))
        return lint(argv + a + 1, argc - a - 1);
    for (;;) {
        if (a + 1 < argc && !strcmp(argv[a], "--timeout")) {
            timeout  = atoi(argv[a + 1]);
            a       += 2;
        } else if (a < argc && !strcmp(argv[a], "--native")) {
            native = 1;
            a++;
        } else {
            break;
        }
    }
    if (argc - a != 2)
        die("usage: testcheck [--timeout SECS] [--native] <cccc> <test.c> | "
            "--lint <test.c>...");
    const char *cccc = argv[a], *file = argv[a + 1];

    Header      h = {0};
    parse_header(&h, file);

    if (native) {
        const char *skip = h.native_skip     ? h.native_skip
                           : h.compile_error ? "expects a compile error"
                           : h.drives_output ? "picks its own output mode"
                                             : NULL;
        if (skip) {
            printf("SKIP %s: %s\n", file, skip);
            return 0;
        }
    }
    // Outside --testing, a native test is built to `bin` and then run.
    int  build_bin = native && !h.testing;
    char bin[1024] = "";
    if (build_bin) {
        const char *tmp = getenv("TMPDIR");
        snprintf(bin, sizeof(bin), "%s/testcheck-XXXXXX",
                 tmp && *tmp ? tmp : "/tmp");
        int fd = mkstemp(bin);
        if (fd < 0)
            die("mkstemp: %s", strerror(errno));
        close(fd);
    }

    char *args[MAX_ARGS * 2 + 8];
    int   n   = 0;
    args[n++] = (char *)cccc;
    // The embedded headers resolve without it.
    if (!native)
        args[n++] = "-I./include";
    for (int i = 0; i < h.nflags; i++)
        args[n++] = native && !strcmp(h.flags[i], "--testing")
                        ? "--testing=native"
                        : h.flags[i];
    if (build_bin) {
        args[n++] = "-c=native";
        args[n++] = "-o";
        args[n++] = bin;
    }
    args[n++] = (char *)file;
    if (h.nrun_args && !build_bin) {
        args[n++] = "--";
        for (int i = 0; i < h.nrun_args; i++)
            args[n++] = h.run_args[i];
    }
    args[n] = NULL;

    char *run[MAX_ARGS + 2];
    int   m  = 0;
    run[m++] = bin;
    for (int i = 0; i < h.nrun_args; i++)
        run[m++] = h.run_args[i];
    run[m]          = NULL;

    Buf         out = {0}, err = {0};
    const char *why     = NULL;
    time_t      start   = time(NULL);
    int         status  = run_capture(args, &out, &err, timeout);
    int         ran_bin = 0;
    if (build_bin && status != -1) {
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            why = "native compile failed";
        } else {
            long left = timeout - (long)(time(NULL) - start);
            status    = left > 0 ? run_capture(run, &out, &err, (int)left) : -1;
            ran_bin   = 1;
        }
    }
    if (build_bin)
        unlink(bin);

    int code = 0;
    if (why) {
        code = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
    } else if (status == -1) {
        why = "timed out";
    } else if (WIFSIGNALED(status)) {
        why  = "killed by a signal";
        code = 128 + WTERMSIG(status);
    } else {
        code = WEXITSTATUS(status);
        if (h.compile_error) {
            Buf both = {0};
            buf_add(&both, out.data ? out.data : "", out.len);
            buf_add(&both, err.data ? err.data : "", err.len);
            if (code == 0 || !has_diagnostic(both.data))
                why = "expected a compile error";
            free(both.data);
        } else if (h.runtime_error) {
            if (code != 255)
                why = "expected a runtime error (exit 255)";
        } else if (!pass_rule(&h, code)) {
            why = h.testing ? "test run failed" : "expected exit 42";
        }
    }
    if (!why && !check_patterns(&h, &out, &err, file))
        why = "";
    if (!why && h.testing && !check_failure_lines(&h, &out, &err, file))
        why = "";
    if (why) {
        if (*why)
            fprintf(stderr, "FAIL %s: %s (exit %d)\n", file, why, code);
        fprintf(stderr, "command:");
        for (int i = 0; args[i]; i++)
            fprintf(stderr, " %s", args[i]);
        if (ran_bin)
            for (int i = 0; run[i]; i++)
                fprintf(stderr, "%s %s", i ? "" : " &&", run[i]);
        fputc('\n', stderr);
        print_tail("stdout", &out);
        print_tail("stderr", &err);
        return 1;
    }
    return 0;
}
