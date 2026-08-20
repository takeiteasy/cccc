/* ndbm.h - legacy database interface for CCCC (#810, #871)
 *
 * macOS/BSD always; Linux when built with CCCC_HAS_NDBM=1 against
 * libgdbm-compat. glibc has never shipped ndbm.h or a dbm_open() symbol in
 * libc itself -- reaching it on Linux means installing libgdbm-compat-dev
 * (a real /usr/include/ndbm.h, byte-identical to gdbm-ndbm.h, plus
 * libgdbm_compat.{so,a}) and linking -lgdbm_compat, an extra dependency
 * for a legacy interface most guest code doesn't need. That's why it's
 * opt-in rather than probed-for-and-silently-enabled: pass
 * CCCC_HAS_NDBM=1 to build.c or the Makefile, which republishes the knob
 * as the guest macro __CCCC_HAS_NDBM__ (see src/preprocess.c). Without the
 * knob this header exists on Linux but declares nothing, matching what a
 * native compiler on that host does (#824's no-lossy-emulation policy).
 *
 * The guest-visible datum { void *dptr; size_t dsize; } stays 16 bytes on
 * every host -- deliberately, even though gdbm's underlying datum narrows
 * dsize to `int` (see src/stdlib/posix.c's wrap_dbm_* host wrappers, which
 * do the narrowing; this implies an INT_MAX cap on value sizes on the
 * Linux path only). This is safe because no datum ever crosses the FFI
 * boundary directly: dbm_fetch()/dbm_firstkey()/dbm_nextkey() return it
 * *by value* and dbm_delete()/dbm_store() take it *by value*, and CCCC's
 * FFI marshalling does not correctly handle a struct/union passed or
 * returned by value through a host call (only a single 64-bit slot is
 * marshalled per argument/return -- see the wrap_semctl comment in
 * src/stdlib/posix.c for the same gap hit by SysV IPC's union semun).
 * Rather than silently mis-marshal the top 8 bytes of a 16-byte datum,
 * the five by-value functions are implemented as `static inline` shims
 * right here in the header: each decomposes its datum arguments into two
 * plain scalar FFI slots (pointer + length) and reassembles the returned
 * datum from two scalar out-parameters written by a `__cccc_dbm_*` helper
 * registered in src/stdlib/posix.c. This keeps the real POSIX signature
 * (`datum dbm_fetch(DBM *, datum)`) fully source-compatible for guest
 * code while every value actually crossing the FFI boundary is a scalar.
 *
 * gdbm's non-POSIX extras (dbm_dirfno, dbm_pagfno, dbm_rdonly) are
 * deliberately not exposed here: no macOS equivalent, not part of the
 * POSIX ndbm surface.
 */

#ifndef __NDBM_H
#define __NDBM_H

#ifdef _WIN32
#error "<ndbm.h> is only available on POSIX targets in CCCC"
#endif

#if defined(__APPLE__) || defined(__CCCC_HAS_NDBM__)

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
extern int dbm_error(DBM *db);
extern int dbm_clearerr(DBM *db);

/* Scalar-only FFI helpers backing the by-value shims below (registered in
   src/stdlib/posix.c, not part of the public ndbm API -- guest code
   should call dbm_fetch/dbm_firstkey/dbm_nextkey/dbm_delete/dbm_store,
   not these directly). */
extern void __cccc_dbm_fetch(DBM *db, const void *kptr, size_t klen,
                             void **out_dptr, size_t *out_dsize);
extern void __cccc_dbm_firstkey(DBM *db, void **out_dptr, size_t *out_dsize);
extern void __cccc_dbm_nextkey(DBM *db, void **out_dptr, size_t *out_dsize);
extern int __cccc_dbm_delete(DBM *db, const void *kptr, size_t klen);
extern int __cccc_dbm_store(DBM *db, const void *kptr, size_t klen,
                            const void *vptr, size_t vlen, int flags);

static inline datum dbm_fetch(DBM *db, datum key) {
    datum r;
    r.dptr  = 0;
    r.dsize = 0;
    __cccc_dbm_fetch(db, key.dptr, key.dsize, &r.dptr, &r.dsize);
    return r;
}

static inline datum dbm_firstkey(DBM *db) {
    datum r;
    r.dptr  = 0;
    r.dsize = 0;
    __cccc_dbm_firstkey(db, &r.dptr, &r.dsize);
    return r;
}

static inline datum dbm_nextkey(DBM *db) {
    datum r;
    r.dptr  = 0;
    r.dsize = 0;
    __cccc_dbm_nextkey(db, &r.dptr, &r.dsize);
    return r;
}

static inline int dbm_delete(DBM *db, datum key) {
    return __cccc_dbm_delete(db, key.dptr, key.dsize);
}

static inline int dbm_store(DBM *db, datum key, datum content, int flags) {
    return __cccc_dbm_store(db, key.dptr, key.dsize, content.dptr,
                            content.dsize, flags);
}

#endif /* __APPLE__ || __CCCC_HAS_NDBM__ */

#endif /* __NDBM_H */
