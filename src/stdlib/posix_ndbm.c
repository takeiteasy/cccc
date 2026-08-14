// posix_ndbm.c -- ndbm.h (macOS/BSD native, or Linux with CCCC_HAS_NDBM)
// (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

#if defined(__APPLE__) || defined(CCCC_HAS_NDBM)
// ndbm.h (#810, #871) -- macOS/BSD natively; on Linux only when built with
// CCCC_HAS_NDBM=1 against libgdbm-compat (see include/ndbm.h). Guarded
// independently of the __linux__ split above (not "not Linux"), since a
// Linux build can go either way depending on the knob.
//
// dbm_fetch/dbm_firstkey/dbm_nextkey/dbm_delete/dbm_store take or return
// `datum` by value; CCCC's FFI marshalling doesn't correctly handle a
// struct/union crossing the host-call boundary by value (only a single
// 64-bit slot is marshalled per argument/return -- same gap as
// wrap_semctl's union semun above), so these five __cccc_dbm_* helpers
// are scalar-only (pointer + length in, pointer + length out) and
// include/ndbm.h's `static inline` shims reassemble the real
// `datum dbm_fetch(DBM*, datum)` shape entirely on the guest side, never
// crossing the FFI boundary with an aggregate.
static long long wrap_dbm_fetch(long long db, long long kptr, long long klen,
                                long long out_dptr, long long out_dsize) {
    datum key = { (void *)(intptr_t)kptr, (size_t)klen };
    datum r = dbm_fetch((DBM *)(intptr_t)db, key);
    *(void **)(intptr_t)out_dptr = r.dptr;
    *(size_t *)(intptr_t)out_dsize = r.dsize;
    return 0;
}

static long long wrap_dbm_firstkey(long long db, long long out_dptr, long long out_dsize) {
    datum r = dbm_firstkey((DBM *)(intptr_t)db);
    *(void **)(intptr_t)out_dptr = r.dptr;
    *(size_t *)(intptr_t)out_dsize = r.dsize;
    return 0;
}

static long long wrap_dbm_nextkey(long long db, long long out_dptr, long long out_dsize) {
    datum r = dbm_nextkey((DBM *)(intptr_t)db);
    *(void **)(intptr_t)out_dptr = r.dptr;
    *(size_t *)(intptr_t)out_dsize = r.dsize;
    return 0;
}

static long long wrap_dbm_delete(long long db, long long kptr, long long klen) {
    datum key = { (void *)(intptr_t)kptr, (size_t)klen };
    return (long long)dbm_delete((DBM *)(intptr_t)db, key);
}

static long long wrap_dbm_store(long long db, long long kptr, long long klen,
                                long long vptr, long long vlen, long long flags) {
    datum key = { (void *)(intptr_t)kptr, (size_t)klen };
    datum content = { (void *)(intptr_t)vptr, (size_t)vlen };
    return (long long)dbm_store((DBM *)(intptr_t)db, key, content, (int)flags);
}

static long long wrap_dbm_open(long long file, long long open_flags, long long file_mode) {
    // gdbm's dbm_open takes a non-const char* (verified in the
    // cccc-linux-amd64 container); macOS's is const char*. Guest code
    // always sees the POSIX `const char *` signature (include/ndbm.h), so
    // the const is cast away here rather than in the guest header.
    return (long long)(intptr_t)dbm_open((char *)(intptr_t)file, (int)open_flags, (mode_t)file_mode);
}

static long long wrap_dbm_close(long long db) {
    dbm_close((DBM *)(intptr_t)db);
    return 0;
}

#ifdef __APPLE__
static long long wrap_dbm_clearerr(long long db) {
    return (long long)dbm_clearerr((DBM *)(intptr_t)db);
}
#else
// gdbm's dbm_clearerr() returns void (not POSIX's int) -- verified in the
// cccc-linux-amd64 container. Always report success to the guest, which
// only ever sees the POSIX `int dbm_clearerr(DBM *)` signature.
static long long wrap_dbm_clearerr(long long db) {
    dbm_clearerr((DBM *)(intptr_t)db);
    return 0;
}
#endif
#endif

void register_posix_ndbm_functions(VirtualMachine *vm) {
#if defined(__APPLE__) || defined(CCCC_HAS_NDBM)
    // ndbm.h (#810, #871) -- macOS/BSD natively, Linux when built with
    // CCCC_HAS_NDBM=1, see include/ndbm.h. The five by-value-datum entry
    // points are registered under their internal __cccc_dbm_* names,
    // matched to include/ndbm.h's extern declarations for the
    // static-inline shims to call. dbm_clearerr is registered as
    // wrap_dbm_clearerr rather than a raw pass-through since gdbm's
    // version returns void, not POSIX's int.
    cc_register_cfunc(vm, "dbm_open",           (void*)wrap_dbm_open,     3, 0);
    cc_register_cfunc(vm, "dbm_close",          (void*)wrap_dbm_close,    1, 0);
    cc_register_cfunc(vm, "dbm_error",          (void*)dbm_error,         1, 0);
    cc_register_cfunc(vm, "dbm_clearerr",       (void*)wrap_dbm_clearerr, 1, 0);
    cc_register_cfunc(vm, "__cccc_dbm_fetch",   (void*)wrap_dbm_fetch,    5, 0);
    cc_register_cfunc(vm, "__cccc_dbm_firstkey",(void*)wrap_dbm_firstkey, 3, 0);
    cc_register_cfunc(vm, "__cccc_dbm_nextkey", (void*)wrap_dbm_nextkey,  3, 0);
    cc_register_cfunc(vm, "__cccc_dbm_delete",  (void*)wrap_dbm_delete,   3, 0);
    cc_register_cfunc(vm, "__cccc_dbm_store",   (void*)wrap_dbm_store,    6, 0);
#else
    (void)vm;
#endif
}

#else
void register_posix_ndbm_functions(VirtualMachine *vm) { (void)vm; }
#endif
