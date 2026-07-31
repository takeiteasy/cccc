/* ndbm.h - legacy database interface for CCCC (#810)
 *
 * macOS/BSD only: glibc has never shipped ndbm.h or a dbm_open() symbol in
 * libc -- verified against both Linux containers (no ndbm.h anywhere
 * under /usr/include, no libgdbm-compat installed). Getting this on Linux
 * would mean adding libgdbm-compat-dev as a new hard build dependency for
 * a legacy interface most guest code doesn't need; deferred as a
 * follow-up rather than done here. On Linux this header exists but
 * declares nothing, matching what a native compiler on that host does
 * (#824's no-lossy-emulation policy).
 *
 * datum { void *dptr; size_t dsize; } is 16 bytes -- byte-identical on
 * every host, but dbm_fetch()/dbm_firstkey()/dbm_nextkey() return it *by
 * value* and dbm_delete()/dbm_store() take it *by value*, and CCCC's FFI
 * marshalling does not correctly handle a struct/union passed or returned
 * by value through a host call (only a single 64-bit slot is marshalled
 * per argument/return -- see the wrap_semctl comment in
 * src/stdlib/posix.c for the same gap hit by SysV IPC's union semun).
 * Rather than silently mis-marshal the top 8 bytes of a 16-byte datum,
 * the five by-value functions are implemented as `static inline` shims
 * right here in the header: each decomposes its datum arguments into two
 * plain scalar FFI slots (pointer + length) and reassembles the returned
 * datum from two scalar out-parameters written by a `__cccc_dbm_*` helper
 * registered in src/stdlib/posix.c. This keeps the real POSIX signature
 * (`datum dbm_fetch(DBM *, datum)`) fully source-compatible for guest
 * code while every value actually crossing the FFI boundary is a scalar.
 */

#ifndef __NDBM_H
#define __NDBM_H

#ifdef _WIN32
#error "<ndbm.h> is only available on POSIX targets in CCCC"
#endif

#ifdef __APPLE__

#include "sys/types.h"
#include "stddef.h"

typedef struct __cccc_DBM DBM;

typedef struct {
    void  *dptr;
    size_t dsize;
} datum;

_Static_assert(sizeof(datum) == 16, "datum layout mismatch");

/* Flags to dbm_store(). */
#define DBM_INSERT  0
#define DBM_REPLACE 1

extern DBM *dbm_open(const char *file, int open_flags, mode_t file_mode);
extern void dbm_close(DBM *db);
extern int  dbm_error(DBM *db);
extern int  dbm_clearerr(DBM *db);

/* Scalar-only FFI helpers backing the by-value shims below (registered in
   src/stdlib/posix.c, not part of the public ndbm API -- guest code
   should call dbm_fetch/dbm_firstkey/dbm_nextkey/dbm_delete/dbm_store,
   not these directly). */
extern void __cccc_dbm_fetch(DBM *db, const void *kptr, size_t klen,
                             void **out_dptr, size_t *out_dsize);
extern void __cccc_dbm_firstkey(DBM *db, void **out_dptr, size_t *out_dsize);
extern void __cccc_dbm_nextkey(DBM *db, void **out_dptr, size_t *out_dsize);
extern int  __cccc_dbm_delete(DBM *db, const void *kptr, size_t klen);
extern int  __cccc_dbm_store(DBM *db, const void *kptr, size_t klen,
                             const void *vptr, size_t vlen, int flags);

static inline datum dbm_fetch(DBM *db, datum key) {
    datum r;
    r.dptr = 0;
    r.dsize = 0;
    __cccc_dbm_fetch(db, key.dptr, key.dsize, &r.dptr, &r.dsize);
    return r;
}

static inline datum dbm_firstkey(DBM *db) {
    datum r;
    r.dptr = 0;
    r.dsize = 0;
    __cccc_dbm_firstkey(db, &r.dptr, &r.dsize);
    return r;
}

static inline datum dbm_nextkey(DBM *db) {
    datum r;
    r.dptr = 0;
    r.dsize = 0;
    __cccc_dbm_nextkey(db, &r.dptr, &r.dsize);
    return r;
}

static inline int dbm_delete(DBM *db, datum key) {
    return __cccc_dbm_delete(db, key.dptr, key.dsize);
}

static inline int dbm_store(DBM *db, datum key, datum content, int flags) {
    return __cccc_dbm_store(db, key.dptr, key.dsize, content.dptr, content.dsize, flags);
}

#endif /* __APPLE__ */

#endif /* __NDBM_H */
