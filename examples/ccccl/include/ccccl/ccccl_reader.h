/* ccccl_reader.h — S-expression reader, comptime-side.
 *
 * Pure C, header-only, fixed arenas -- see the file comment in
 * ccccl_plan.h for why (compiled twice: plain `cc` for tests, and inside
 * the cccc comptime VM). This reader produces its own private form-tree
 * representation (CccclForm); it has nothing to do with the runtime's LObj,
 * which exists only in the generated program, not at compile time.
 *
 * `fopen`/`fgetc`/`fclose` are used directly and are confirmed to work
 * inside cccc's comptime VM once the including file routes them with
 * `#include @comptime <stdio.h>` (see docs/ARCHITECTURE.md).
 */
#ifndef CCCCL_READER_H
#define CCCCL_READER_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CL_READ_MAX_FORMS 4096
#define CL_READ_MAX_ATOM_CHARS 65536
#define CL_READ_MAX_LEN 64

typedef enum CccclFormKind { CL_FORM_ATOM, CL_FORM_PAIR } CccclFormKind;

typedef struct CccclForm CccclForm;
struct CccclForm {
    CccclFormKind kind;
    char atom[CL_READ_MAX_LEN]; /* upper-cased, matching SectorLISP style */
    CccclForm *car, *cdr;       /* PAIR only; NULL cdr means the list end   */
};

typedef struct {
    CccclForm forms[CL_READ_MAX_FORMS];
    int form_count;

    CccclForm *nil; /* the interned empty-list/NIL form */

    char error[256];
    int has_error;
} CccclReader;

static CccclForm *ccccl_read_alloc(CccclReader *r) {
    if (r->form_count >= CL_READ_MAX_FORMS) {
        if (!r->has_error) {
            snprintf(r->error, sizeof(r->error), "reader: form arena exhausted");
            r->has_error = 1;
        }
        return r->nil;
    }
    return &r->forms[r->form_count++];
}

static void ccccl_reader_init(CccclReader *r) {
    r->form_count = 0;
    r->error[0] = '\0';
    r->has_error = 0;
    r->nil = ccccl_read_alloc(r);
    r->nil->kind = CL_FORM_ATOM;
    r->nil->car = r->nil->cdr = NULL;
    {
        int i;
        for (i = 0; i < CL_READ_MAX_LEN; i++) r->nil->atom[i] = '\0';
        r->nil->atom[0] = 'N'; r->nil->atom[1] = 'I'; r->nil->atom[2] = 'L';
    }
}

/* --- tokenizer state over an in-memory buffer --------------------------- */

typedef struct {
    const char *buf;
    long pos, len;
} CccclLexer;

static int ccccl_lex_peek(CccclLexer *lx) {
    return lx->pos < lx->len ? (unsigned char)lx->buf[lx->pos] : -1;
}

static void ccccl_lex_skip_ws(CccclLexer *lx) {
    for (;;) {
        int c = ccccl_lex_peek(lx);
        if (c == ';') { /* line comment */
            while (c != -1 && c != '\n') { lx->pos++; c = ccccl_lex_peek(lx); }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { lx->pos++; continue; }
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
            snprintf(r->error, sizeof(r->error), "reader: unexpected end of file in list");
            r->has_error = 1;
        }
        return r->nil;
    }
    {
        CccclForm *head = ccccl_read_alloc(r);
        head->kind = CL_FORM_PAIR;
        head->car = ccccl_read_form(r, lx);
        head->cdr = ccccl_read_list(r, lx);
        return head;
    }
}

static CccclForm *ccccl_read_form(CccclReader *r, CccclLexer *lx) {
    ccccl_lex_skip_ws(lx);
    {
        int c = ccccl_lex_peek(lx);
        if (c == -1) {
            if (!r->has_error) {
                snprintf(r->error, sizeof(r->error), "reader: unexpected end of file");
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
            int n = 0;
            while (!ccccl_is_delim(ccccl_lex_peek(lx))) {
                int ch = ccccl_lex_peek(lx);
                if (n < CL_READ_MAX_LEN - 1) {
                    if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A'; /* uppercase */
                    buf[n++] = (char)ch;
                }
                lx->pos++;
            }
            buf[n] = '\0';
            {
                CccclForm *a = ccccl_read_alloc(r);
                int i;
                a->kind = CL_FORM_ATOM;
                a->car = a->cdr = NULL;
                for (i = 0; i <= n; i++) a->atom[i] = buf[i];
                return a;
            }
        }
    }
}

/* Reads every top-level form in `path` into `out[]` (capacity `cap`),
 * returning the count, or -1 with r->error set on failure to open. */
static int ccccl_read_file(CccclReader *r, const char *path,
                            CccclForm **out, int cap) {
    FILE *f = fopen(path, "rb");
    static char buf[CL_READ_MAX_ATOM_CHARS];
    long len;
    int n = 0;

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
            if (ccccl_lex_peek(&lx) == -1) break;
            if (n >= cap) {
                snprintf(r->error, sizeof(r->error), "reader: too many top-level forms");
                r->has_error = 1;
                break;
            }
            out[n++] = ccccl_read_form(r, &lx);
            if (r->has_error) break;
        }
    }
    return r->has_error ? -1 : n;
}

#ifdef __cplusplus
}
#endif

#endif /* CCCCL_READER_H */
