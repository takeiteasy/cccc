/* ccccl_form.h — S-expression reader, comptime-side (declarations).
 *
 * Pure C. The definitions live in src/ccccl_form.c, compiled twice: once by
 * plain `cc` (linked into the example binaries), once inside the cccc
 * comptime VM — src/ccccl_comptime.c pulls these declarations in with
 * `#include @comptime "ccccl_form.h"` and cccc forwards the bodies into the
 * comptime program on demand.
 *
 * This reader produces its own private form-tree representation (CccclForm);
 * it has nothing to do with the runtime's LObj, which exists only in the
 * generated program, not at compile time.
 *
 * Three form kinds: ATOM (a symbol, upper-cased -- `append` reads as
 * `APPEND`), PAIR (a cons cell), and INT (a fixnum literal: an optional
 * leading `-` followed by one or more digits, delimiter-terminated the
 * same way an atom is).
 */
#ifndef CCCCL_FORM_H
#define CCCCL_FORM_H

#ifdef __cplusplus
extern "C" {
#endif

#define CL_READ_MAX_FORMS      4096
#define CL_READ_MAX_ATOM_CHARS 65536
#define CL_READ_MAX_LEN        64

typedef enum CccclFormKind {
    CL_FORM_ATOM,
    CL_FORM_PAIR,
    CL_FORM_INT
} CccclFormKind;

typedef struct CccclForm CccclForm;
struct CccclForm {
    CccclFormKind kind;
    char          atom[CL_READ_MAX_LEN]; /* ATOM only, upper-cased */
    long long     ival;                  /* INT only */
    CccclForm    *car, *cdr; /* PAIR only; NULL cdr means the list end */
};

typedef struct {
    CccclForm  forms[CL_READ_MAX_FORMS];
    int        form_count;

    CccclForm *nil; /* the interned empty-list/NIL form */

    char       error[256];
    int        has_error;
} CccclReader;

void ccccl_reader_init(CccclReader *r);

/* Reads every top-level form in `path` into `out[]` (capacity `cap`),
 * returning the count, or -1 with r->error set on failure to open. */
int ccccl_read_file(CccclReader *r, const char *path, CccclForm **out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* CCCCL_FORM_H */
