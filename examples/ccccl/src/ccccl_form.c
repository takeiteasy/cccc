/* ccccl_form.c — S-expression reader, definitions for ccccl_form.h.
 *
 * Pure C. Compiled twice: once by plain `cc` (linked into the example
 * binaries), once inside the cccc comptime VM via src/ccccl_comptime.c's
 * `#include @comptime "ccccl_form.h"` + on-demand body forwarding.
 *
 * `fopen`/`fread`/`fclose` are used directly and are confirmed to work
 * inside cccc's comptime VM once src/ccccl_comptime.c routes them with
 * `#include @comptime <stdio.h>`.
 */
#include "ccccl_form.h"

#include <stdio.h>

static CccclForm *ccccl_read_alloc(CccclReader *r) {
    if (r->form_count >= CL_READ_MAX_FORMS) {
        if (!r->has_error) {
            snprintf(r->error, sizeof(r->error),
                     "reader: form arena exhausted");
            r->has_error = 1;
        }
        return r->nil;
    }
    return &r->forms[r->form_count++];
}

void ccccl_reader_init(CccclReader *r) {
    r->form_count = 0;
    r->error[0]   = '\0';
    r->has_error  = 0;
    r->nil        = ccccl_read_alloc(r);
    r->nil->kind  = CL_FORM_ATOM;
    r->nil->car = r->nil->cdr = NULL;
    {
        int i;
        for (i = 0; i < CL_READ_MAX_LEN; i++)
            r->nil->atom[i] = '\0';
        r->nil->atom[0] = 'N';
        r->nil->atom[1] = 'I';
        r->nil->atom[2] = 'L';
    }
}

/* --- tokenizer state over an in-memory buffer --------------------------- */

typedef struct {
    const char *buf;
    long        pos, len;
} CccclLexer;

static int ccccl_lex_peek(CccclLexer *lx) {
    return lx->pos < lx->len ? (unsigned char)lx->buf[lx->pos] : -1;
}

static void ccccl_lex_skip_ws(CccclLexer *lx) {
    for (;;) {
        int c = ccccl_lex_peek(lx);
        if (c == ';') { /* line comment */
            while (c != -1 && c != '\n') {
                lx->pos++;
                c = ccccl_lex_peek(lx);
            }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            lx->pos++;
            continue;
        }
        break;
    }
}

static int ccccl_is_delim(int c) {
    return c == -1 || c == '(' || c == ')' || c == ' ' || c == '\t' ||
           c == '\n' || c == '\r' || c == ';';
}

static CccclForm *ccccl_read_form(CccclReader *r, CccclLexer *lx);

static CccclForm *ccccl_read_list(CccclReader *r, CccclLexer *lx) {
    ccccl_lex_skip_ws(lx);
    if (ccccl_lex_peek(lx) == ')') {
        lx->pos++;
        return r->nil;
    }
    if (ccccl_lex_peek(lx) == -1) {
        if (!r->has_error) {
            snprintf(r->error, sizeof(r->error),
                     "reader: unexpected end of file in list");
            r->has_error = 1;
        }
        return r->nil;
    }
    {
        CccclForm *head = ccccl_read_alloc(r);
        head->kind      = CL_FORM_PAIR;
        head->car       = ccccl_read_form(r, lx);
        head->cdr       = ccccl_read_list(r, lx);
        return head;
    }
}

/* An atom token is an INT iff it is an optional leading '-' followed by at
 * least one digit and nothing else -- `-` alone, or `1a`, stays an ATOM
 * (matching a Lisp reader's usual "a lone sign or any non-digit tail keeps
 * it a symbol" rule). */
static int ccccl_token_is_int(const char *buf, int n, long long *out) {
    int       i   = 0;
    int       neg = 0;
    long long v   = 0;
    if (n == 0)
        return 0;
    if (buf[0] == '-') {
        neg = 1;
        i   = 1;
    }
    if (i >= n)
        return 0; /* lone '-' */
    for (; i < n; i++) {
        if (buf[i] < '0' || buf[i] > '9')
            return 0;
        v = v * 10 + (buf[i] - '0');
    }
    *out = neg ? -v : v;
    return 1;
}

static CccclForm *ccccl_read_form(CccclReader *r, CccclLexer *lx) {
    ccccl_lex_skip_ws(lx);
    {
        int c = ccccl_lex_peek(lx);
        if (c == -1) {
            if (!r->has_error) {
                snprintf(r->error, sizeof(r->error),
                         "reader: unexpected end of file");
                r->has_error = 1;
            }
            return r->nil;
        }
        if (c == '(') {
            lx->pos++;
            return ccccl_read_list(r, lx);
        }
        if (c == ')') {
            if (!r->has_error) {
                snprintf(r->error, sizeof(r->error), "reader: unexpected ')'");
                r->has_error = 1;
            }
            lx->pos++;
            return r->nil;
        }
        {
            char buf[CL_READ_MAX_LEN];
            int  n = 0;
            while (!ccccl_is_delim(ccccl_lex_peek(lx))) {
                int ch = ccccl_lex_peek(lx);
                if (n < CL_READ_MAX_LEN - 1)
                    buf[n++] = (char)ch;
                lx->pos++;
            }
            {
                long long ival;
                if (ccccl_token_is_int(buf, n, &ival)) {
                    CccclForm *a = ccccl_read_alloc(r);
                    a->kind      = CL_FORM_INT;
                    a->ival      = ival;
                    a->car = a->cdr = NULL;
                    return a;
                }
            }
            {
                CccclForm *a = ccccl_read_alloc(r);
                int        i;
                a->kind = CL_FORM_ATOM;
                a->car = a->cdr = NULL;
                for (i = 0; i < n; i++) {
                    char ch = buf[i];
                    if (ch >= 'a' && ch <= 'z')
                        ch = (char)(ch - 'a' + 'A');
                    a->atom[i] = ch;
                }
                a->atom[n] = '\0';
                return a;
            }
        }
    }
}

int ccccl_read_file(CccclReader *r, const char *path, CccclForm **out,
                    int cap) {
    FILE       *f = fopen(path, "rb");
    static char buf[CL_READ_MAX_ATOM_CHARS];
    long        len;
    int         n = 0;

    if (!f) {
        snprintf(r->error, sizeof(r->error), "reader: cannot open '%s'", path);
        r->has_error = 1;
        return -1;
    }
    len = (long)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = '\0';

    {
        CccclLexer lx;
        lx.buf = buf;
        lx.pos = 0;
        lx.len = len;
        while (1) {
            ccccl_lex_skip_ws(&lx);
            if (ccccl_lex_peek(&lx) == -1)
                break;
            if (n >= cap) {
                snprintf(r->error, sizeof(r->error),
                         "reader: too many top-level forms");
                r->has_error = 1;
                break;
            }
            out[n++] = ccccl_read_form(r, &lx);
            if (r->has_error)
                break;
        }
    }
    return r->has_error ? -1 : n;
}
