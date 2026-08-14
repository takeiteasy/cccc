// posix_net.c -- sockets, DNS/NSS lookups (+ portable _r shims / Linux
// native _r dispatch), byte-order/inet helpers, and related net-adjacent
// registrations (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

static long long wrap_accept_gil(long long sockfd, long long addr, long long addrlen) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)accept((int)sockfd, (struct sockaddr *)addr, (socklen_t *)addrlen);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = accept((int)sockfd, (struct sockaddr *)addr, (socklen_t *)addrlen);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_connect_gil(long long sockfd, long long addr, long long addrlen) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)connect((int)sockfd, (const struct sockaddr *)addr, (socklen_t)addrlen);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = connect((int)sockfd, (const struct sockaddr *)addr, (socklen_t)addrlen);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_recv_gil(long long sockfd, long long buf, long long len, long long flags) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)recv((int)sockfd, (void *)buf, (size_t)len, (int)flags);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = recv((int)sockfd, (void *)buf, (size_t)len, (int)flags);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_send_gil(long long sockfd, long long buf, long long len, long long flags) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)send((int)sockfd, (const void *)buf, (size_t)len, (int)flags);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = send((int)sockfd, (const void *)buf, (size_t)len, (int)flags);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_recvfrom_gil(long long sockfd, long long buf, long long len, long long flags,
                                    long long addr, long long addrlen) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)recvfrom((int)sockfd, (void *)buf, (size_t)len, (int)flags,
                                    (struct sockaddr *)addr, (socklen_t *)addrlen);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = recvfrom((int)sockfd, (void *)buf, (size_t)len, (int)flags,
                          (struct sockaddr *)addr, (socklen_t *)addrlen);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_sendto_gil(long long sockfd, long long buf, long long len, long long flags,
                                  long long addr, long long addrlen) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)sendto((int)sockfd, (const void *)buf, (size_t)len, (int)flags,
                                  (const struct sockaddr *)addr, (socklen_t)addrlen);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = sendto((int)sockfd, (const void *)buf, (size_t)len, (int)flags,
                        (const struct sockaddr *)addr, (socklen_t)addrlen);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_sendmsg_gil(long long sockfd, long long msg, long long flags) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)sendmsg((int)sockfd, (const struct msghdr *)msg, (int)flags);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = sendmsg((int)sockfd, (const struct msghdr *)msg, (int)flags);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_recvmsg_gil(long long sockfd, long long msg, long long flags) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)recvmsg((int)sockfd, (struct msghdr *)msg, (int)flags);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = recvmsg((int)sockfd, (struct msghdr *)msg, (int)flags);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// ---------------------------------------------------------------------------
// DNS/NSS lookup wrappers (#748) — release the GIL while the call may block
//
// gethostbyname/gethostbyaddr/getaddrinfo/getnameinfo/getnetbyname/
// getnetbyaddr were previously direct (non-GIL-releasing) FFI calls, even
// though real DNS/NSS lookups can block for seconds. Under the VM's
// single-GIL threading model this stalled every other VM thread for the
// duration. Now they follow the same posix_save_and_release_gil /
// posix_acquire_and_restore_gil pattern as recv/send/waitpid/waitid.
//
// Caveat: gethostbyname/gethostbyaddr/getnetbyname/getnetbyaddr return
// pointers into static, non-reentrant host storage. Releasing the GIL means
// two VM threads can now race on that shared buffer where the GIL previously
// serialized them -- inherent to this POSIX API (the _r variants exist for
// exactly this reason); the guest-visible symptom would be one thread
// reading a result that another thread's later call has already overwritten,
// not a hang or corruption of unrelated memory. getaddrinfo/getnameinfo are
// reentrant and unaffected.
//
// nss_static_mutex (#785) additionally serializes these against the
// gethostbyname_r/gethostbyaddr_r/getnetbyname_r *portable shim* below, so
// the static buffer is at least never *written* concurrently -- the plain
// functions' returned pointer is still only valid until the next call from
// any thread on any of these six functions, which the mutex cannot fix;
// that's what the _r variants are for. On Linux the _r functions instead
// forward straight to glibc's own native _r variants (#791), which touch no
// static storage at all and therefore never take this mutex.
#if !defined(_WIN32) && !defined(_WIN64)
static pthread_mutex_t nss_static_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static long long wrap_gethostbyname_gil(long long name) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)gethostbyname((const char *)name);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct hostent *r = gethostbyname((const char *)name);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_gethostbyaddr_gil(long long addr, long long len, long long type) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)gethostbyaddr((const void *)addr, (socklen_t)len, (int)type);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct hostent *r = gethostbyaddr((const void *)addr, (socklen_t)len, (int)type);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_getaddrinfo_gil(long long node, long long service, long long hints, long long res) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)getaddrinfo((const char *)node, (const char *)service,
                                       (const struct addrinfo *)hints, (struct addrinfo **)res);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = getaddrinfo((const char *)node, (const char *)service,
                         (const struct addrinfo *)hints, (struct addrinfo **)res);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_getnameinfo_gil(long long addr, long long addrlen, long long host, long long hostlen,
                                       long long serv, long long servlen, long long flags) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)getnameinfo((const struct sockaddr *)addr, (socklen_t)addrlen,
                                       (char *)host, (socklen_t)hostlen,
                                       (char *)serv, (socklen_t)servlen, (int)flags);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = getnameinfo((const struct sockaddr *)addr, (socklen_t)addrlen,
                         (char *)host, (socklen_t)hostlen,
                         (char *)serv, (socklen_t)servlen, (int)flags);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_getnetbyname_gil(long long name) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)getnetbyname((const char *)name);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct netent *r = getnetbyname((const char *)name);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_getnetbyaddr_gil(long long net, long long type) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)getnetbyaddr((uint32_t)net, (int)type);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct netent *r = getnetbyaddr((uint32_t)net, (int)type);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// ---------------------------------------------------------------------------
// gethostbyname_r/gethostbyaddr_r/getnetbyname_r (#785) — race-free
// alternative to the static-buffer lookups above.
//
// macOS has no gethostbyname_r/gethostbyaddr_r/getnetbyname_r at all (glibc-
// only extensions), so this is one portable shim used on both platforms
// rather than a passthrough to the host's own _r functions: nss_static_mutex
// serializes access to the underlying plain lookup's static buffer (taken
// here AND by the plain wrappers above, so the two families are mutually
// exclusive), and the result is deep-copied into the caller's own buf before
// the mutex is released. This closes the write race the #748 GIL release
// introduced -- concurrent guest threads can no longer see a torn or
// overwritten struct -- but the plain wrappers' *returned pointer* is still
// only valid until the next call from any thread on any of these six
// functions; that part is unfixable for those and is exactly why the _r
// variants exist. See followup ticket for forwarding to glibc's native _r
// functions on Linux instead of this shim, for true reentrancy without
// serialization. nss_static_mutex itself is declared above, next to the
// plain wrappers it also protects.
//
// This shim (and its nss_r_layout_size/nss_r_copy_ptr_array/nss_count_list
// helpers) is only compiled where it is actually used -- everywhere except
// Linux, which forwards to glibc's native _r functions instead (#791,
// wrap_gethostbyname_r_gil etc. below) and would otherwise leave these as
// unused statics.
#ifndef __linux__

// Required buffer size for a NULL-terminated char* array of `count` non-NULL
// entries (whose combined string bytes, each including its NUL, are
// `str_bytes`) plus alignment padding for the pointer array itself.
static size_t nss_r_layout_size(int count, size_t str_bytes) {
    size_t ptrs = (size_t)(count + 1) * sizeof(char *);
    return ptrs + sizeof(char *) /* worst-case alignment padding */ + str_bytes;
}

// Copies a NULL-terminated `char **list` (with `count` entries) into `buf`,
// writing the new pointer array at *cursor (advanced past the array +
// alignment) and each string right after. Returns the new array's base, or
// NULL if `end` would be exceeded.
static char **nss_r_copy_ptr_array(char **list, int count, char **cursor, char *end) {
    char *p = (char *)*cursor;
    /* Align the pointer-array base to sizeof(char *). */
    p = (char *)(((uintptr_t)p + sizeof(char *) - 1) & ~(uintptr_t)(sizeof(char *) - 1));
    char **arr = (char **)(void *)p;
    if ((char *)(arr + count + 1) > end) return NULL;
    char *strp = (char *)(arr + count + 1);
    for (int i = 0; i < count; i++) {
        size_t len = strlen(list[i]) + 1;
        if (strp + len > end) return NULL;
        memcpy(strp, list[i], len);
        arr[i] = strp;
        strp += len;
    }
    arr[count] = 0;
    *cursor = strp;
    return arr;
}

static int nss_count_list(char **list) {
    int n = 0;
    if (list) while (list[n]) n++;
    return n;
}

static long long nss_gethostbyname_r_shim(long long name, long long ret, long long buf,
                                           long long buflen, long long result, long long h_errnop) {
    struct hostent *out = (struct hostent *)ret;
    char *b = (char *)buf;
    size_t blen = (size_t)buflen;
    struct hostent **resultp = (struct hostent **)result;
    int *herrp = (int *)h_errnop;

    VirtualMachine *vm = cccc_posix_current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) cccc_posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct hostent *src = gethostbyname((const char *)name);
    int rc = 0;
    if (!src) {
        *resultp = 0;
        if (herrp) *herrp = HOST_NOT_FOUND;
    } else {
        int naliases = nss_count_list(src->h_aliases);
        int naddrs = nss_count_list(src->h_addr_list);
        size_t need = strlen(src->h_name) + 1;
        for (int i = 0; i < naliases; i++) need += strlen(src->h_aliases[i]) + 1;
        need = nss_r_layout_size(naliases, need) +
               nss_r_layout_size(naddrs, (size_t)naddrs * (size_t)src->h_length);
        if (need > blen) {
            rc = ERANGE;
            *resultp = 0;
        } else {
            char *cursor = b;
            char *end = b + blen;
            /* Name string first. */
            size_t namelen = strlen(src->h_name) + 1;
            if (cursor + namelen > end) { rc = ERANGE; *resultp = 0; goto done; }
            memcpy(cursor, src->h_name, namelen);
            out->h_name = cursor;
            cursor += namelen;

            char **aliases = nss_r_copy_ptr_array(src->h_aliases, naliases, &cursor, end);
            if (!aliases) { rc = ERANGE; *resultp = 0; goto done; }
            out->h_aliases = aliases;

            out->h_addrtype = src->h_addrtype;
            out->h_length = src->h_length;

            /* Address list: not NUL-terminated strings but fixed h_length
               byte blobs -- copy manually rather than via
               nss_r_copy_ptr_array's strlen-based packer. */
            char *p = cursor;
            p = (char *)(((uintptr_t)p + sizeof(char *) - 1) & ~(uintptr_t)(sizeof(char *) - 1));
            char **addrs = (char **)(void *)p;
            if ((char *)(addrs + naddrs + 1) > end) { rc = ERANGE; *resultp = 0; goto done; }
            char *ap = (char *)(addrs + naddrs + 1);
            for (int i = 0; i < naddrs; i++) {
                if (ap + src->h_length > end) { rc = ERANGE; *resultp = 0; goto done; }
                memcpy(ap, src->h_addr_list[i], (size_t)src->h_length);
                addrs[i] = ap;
                ap += src->h_length;
            }
            addrs[naddrs] = 0;
            out->h_addr_list = addrs;

            *resultp = out;
        }
    }
done:
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    if (gil) cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
}

static long long nss_gethostbyaddr_r_shim(long long addr, long long len, long long type, long long ret,
                                           long long buf, long long buflen, long long result, long long h_errnop) {
    struct hostent *out = (struct hostent *)ret;
    char *b = (char *)buf;
    size_t blen = (size_t)buflen;
    struct hostent **resultp = (struct hostent **)result;
    int *herrp = (int *)h_errnop;

    VirtualMachine *vm = cccc_posix_current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) cccc_posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct hostent *src = gethostbyaddr((const void *)addr, (socklen_t)len, (int)type);
    int rc = 0;
    if (!src) {
        *resultp = 0;
        if (herrp) *herrp = HOST_NOT_FOUND;
    } else {
        int naliases = nss_count_list(src->h_aliases);
        int naddrs = nss_count_list(src->h_addr_list);
        size_t need = strlen(src->h_name) + 1;
        for (int i = 0; i < naliases; i++) need += strlen(src->h_aliases[i]) + 1;
        need = nss_r_layout_size(naliases, need) +
               nss_r_layout_size(naddrs, (size_t)naddrs * (size_t)src->h_length);
        if (need > blen) {
            rc = ERANGE;
            *resultp = 0;
        } else {
            char *cursor = b;
            char *end = b + blen;
            size_t namelen = strlen(src->h_name) + 1;
            if (cursor + namelen > end) { rc = ERANGE; *resultp = 0; goto done2; }
            memcpy(cursor, src->h_name, namelen);
            out->h_name = cursor;
            cursor += namelen;

            char **aliases = nss_r_copy_ptr_array(src->h_aliases, naliases, &cursor, end);
            if (!aliases) { rc = ERANGE; *resultp = 0; goto done2; }
            out->h_aliases = aliases;

            out->h_addrtype = src->h_addrtype;
            out->h_length = src->h_length;

            char *p = cursor;
            p = (char *)(((uintptr_t)p + sizeof(char *) - 1) & ~(uintptr_t)(sizeof(char *) - 1));
            char **addrs = (char **)(void *)p;
            if ((char *)(addrs + naddrs + 1) > end) { rc = ERANGE; *resultp = 0; goto done2; }
            char *ap = (char *)(addrs + naddrs + 1);
            for (int i = 0; i < naddrs; i++) {
                if (ap + src->h_length > end) { rc = ERANGE; *resultp = 0; goto done2; }
                memcpy(ap, src->h_addr_list[i], (size_t)src->h_length);
                addrs[i] = ap;
                ap += src->h_length;
            }
            addrs[naddrs] = 0;
            out->h_addr_list = addrs;

            *resultp = out;
        }
    }
done2:
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    if (gil) cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
}

static long long nss_getnetbyname_r_shim(long long name, long long ret, long long buf,
                                          long long buflen, long long result, long long h_errnop) {
    struct netent *out = (struct netent *)ret;
    char *b = (char *)buf;
    size_t blen = (size_t)buflen;
    struct netent **resultp = (struct netent **)result;
    int *herrp = (int *)h_errnop;

    VirtualMachine *vm = cccc_posix_current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) cccc_posix_save_and_release_gil(vm, &state);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock(&nss_static_mutex);
#endif
    struct netent *src = getnetbyname((const char *)name);
    int rc = 0;
    if (!src) {
        *resultp = 0;
        if (herrp) *herrp = HOST_NOT_FOUND;
    } else {
        int naliases = nss_count_list(src->n_aliases);
        size_t need = strlen(src->n_name) + 1;
        for (int i = 0; i < naliases; i++) need += strlen(src->n_aliases[i]) + 1;
        need = nss_r_layout_size(naliases, need);
        if (need > blen) {
            rc = ERANGE;
            *resultp = 0;
        } else {
            char *cursor = b;
            char *end = b + blen;
            size_t namelen = strlen(src->n_name) + 1;
            if (cursor + namelen > end) { rc = ERANGE; *resultp = 0; goto done3; }
            memcpy(cursor, src->n_name, namelen);
            out->n_name = cursor;
            cursor += namelen;

            char **aliases = nss_r_copy_ptr_array(src->n_aliases, naliases, &cursor, end);
            if (!aliases) { rc = ERANGE; *resultp = 0; goto done3; }
            out->n_aliases = aliases;

            out->n_addrtype = src->n_addrtype;
            out->n_net = src->n_net;

            *resultp = out;
        }
    }
done3:
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock(&nss_static_mutex);
#endif
    if (gil) cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
}

#endif /* !__linux__ */

// ---------------------------------------------------------------------------
// gethostbyname_r/gethostbyaddr_r/getnetbyname_r dispatch (#791): on Linux,
// forward straight to glibc's own native _r variants instead of the
// portable shim above. glibc's _r functions use no static/shared storage at
// all (that's the entire point of the _r family), so this path deliberately
// takes no lock -- unlike the shim, which serializes every guest thread's
// lookup through nss_static_mutex. Guest-visible signature/behavior (return
// value, *result, *h_errnop, ERANGE on a too-small buffer) is unchanged;
// this is a Linux-only implementation swap for less contention, not an API
// change (see the deferred-from-#785 followup that filed this). Forward-
// declared locally rather than flipping on _GNU_SOURCE for the whole TU --
// same gap class as mremap/fallocate/splice/ppoll above -- glibc exports
// these regardless of feature-test macros. macOS and every other platform
// keep using the portable shim, since they have no native _r variants at
// all (glibc-only extensions).
//
// One deliberate behavior refinement on the Linux path: on a not-found
// lookup, the portable shim above always sets *h_errnop = HOST_NOT_FOUND,
// whereas glibc's native _r functions set *h_errnop to whichever of
// HOST_NOT_FOUND/TRY_AGAIN/NO_RECOVERY/NO_DATA actually applies -- strictly
// more accurate, not a regression (the shim's HOST_NOT_FOUND-only behavior
// was never a documented guarantee, just what the shim happened to do).
#ifdef __linux__
extern int gethostbyname_r(const char *name, struct hostent *ret,
                           char *buf, size_t buflen,
                           struct hostent **result, int *h_errnop);
extern int gethostbyaddr_r(const void *addr, socklen_t len, int type,
                           struct hostent *ret, char *buf, size_t buflen,
                           struct hostent **result, int *h_errnop);
extern int getnetbyname_r(const char *name, struct netent *ret,
                          char *buf, size_t buflen,
                          struct netent **result, int *h_errnop);
#endif

static long long wrap_gethostbyname_r_gil(long long name, long long ret, long long buf,
                                           long long buflen, long long result, long long h_errnop) {
#ifdef __linux__
    VirtualMachine *vm = cccc_posix_current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) cccc_posix_save_and_release_gil(vm, &state);
    int rc = gethostbyname_r((const char *)name, (struct hostent *)ret, (char *)buf,
                             (size_t)buflen, (struct hostent **)result, (int *)h_errnop);
    if (gil) cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
#else
    return nss_gethostbyname_r_shim(name, ret, buf, buflen, result, h_errnop);
#endif
}

static long long wrap_gethostbyaddr_r_gil(long long addr, long long len, long long type, long long ret,
                                           long long buf, long long buflen, long long result, long long h_errnop) {
#ifdef __linux__
    VirtualMachine *vm = cccc_posix_current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) cccc_posix_save_and_release_gil(vm, &state);
    int rc = gethostbyaddr_r((const void *)addr, (socklen_t)len, (int)type,
                             (struct hostent *)ret, (char *)buf, (size_t)buflen,
                             (struct hostent **)result, (int *)h_errnop);
    if (gil) cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
#else
    return nss_gethostbyaddr_r_shim(addr, len, type, ret, buf, buflen, result, h_errnop);
#endif
}

static long long wrap_getnetbyname_r_gil(long long name, long long ret, long long buf,
                                          long long buflen, long long result, long long h_errnop) {
#ifdef __linux__
    VirtualMachine *vm = cccc_posix_current_vm();
    ExecState state;
    int gil = vm && vm->gil_initialized;
    if (gil) cccc_posix_save_and_release_gil(vm, &state);
    int rc = getnetbyname_r((const char *)name, (struct netent *)ret, (char *)buf,
                            (size_t)buflen, (struct netent **)result, (int *)h_errnop);
    if (gil) cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)rc;
#else
    return nss_getnetbyname_r_shim(name, ret, buf, buflen, result, h_errnop);
#endif
}

static long long wrap_htonl(long long hostlong) { return (long long)htonl((uint32_t)hostlong); }
static long long wrap_htons(long long hostshort) { return (long long)htons((uint16_t)hostshort); }
static long long wrap_ntohl(long long netlong) { return (long long)ntohl((uint32_t)netlong); }
static long long wrap_ntohs(long long netshort) { return (long long)ntohs((uint16_t)netshort); }
static long long wrap_inet_addr(long long cp) { return (long long)inet_addr((const char *)cp); }
static long long wrap_bzero(long long s, long long n) { bzero((void *)s, (size_t)n); return 0; }
static long long wrap_bcopy(long long src, long long dst, long long n) { bcopy((const void *)src, (void *)dst, (size_t)n); return 0; }
static long long wrap_freeaddrinfo(long long res) { freeaddrinfo((struct addrinfo *)res); return 0; }

void register_posix_net_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "accept",  (void*)wrap_accept_gil,  3, 0);
    cc_register_cfunc(vm, "connect", (void*)wrap_connect_gil, 3, 0);
    cc_register_cfunc(vm, "recv",     (void*)wrap_recv_gil,     4, 0);
    cc_register_cfunc(vm, "send",     (void*)wrap_send_gil,     4, 0);
    cc_register_cfunc(vm, "recvfrom", (void*)wrap_recvfrom_gil, 6, 0);
    cc_register_cfunc(vm, "sendto",   (void*)wrap_sendto_gil,   6, 0);
    cc_register_cfunc(vm, "sendmsg",  (void*)wrap_sendmsg_gil,  3, 0);
    cc_register_cfunc(vm, "recvmsg",  (void*)wrap_recvmsg_gil,  3, 0);
    // DNS/NSS lookups (#748) — real resolver calls can block for seconds
    cc_register_cfunc(vm, "gethostbyname",(void*)wrap_gethostbyname_gil, 1, 0);
    cc_register_cfunc(vm, "gethostbyaddr",(void*)wrap_gethostbyaddr_gil, 3, 0);
    cc_register_cfunc(vm, "getaddrinfo", (void*)wrap_getaddrinfo_gil,    4, 0);
    cc_register_cfunc(vm, "getnameinfo", (void*)wrap_getnameinfo_gil,    7, 0);
    cc_register_cfunc(vm, "getnetbyname",(void*)wrap_getnetbyname_gil,   1, 0);
    cc_register_cfunc(vm, "getnetbyaddr",(void*)wrap_getnetbyaddr_gil,   2, 0);
    // Race-free _r variants (#785)
    cc_register_cfunc(vm, "gethostbyname_r",(void*)wrap_gethostbyname_r_gil, 6, 0);
    cc_register_cfunc(vm, "gethostbyaddr_r",(void*)wrap_gethostbyaddr_r_gil, 8, 0);
    cc_register_cfunc(vm, "getnetbyname_r", (void*)wrap_getnetbyname_r_gil,  6, 0);

    cc_register_cfunc(vm, "htonl",   (void*)wrap_htonl,  1, 0);
    cc_register_cfunc(vm, "htons",   (void*)wrap_htons,  1, 0);
    cc_register_cfunc(vm, "ntohl",   (void*)wrap_ntohl,  1, 0);
    cc_register_cfunc(vm, "ntohs",   (void*)wrap_ntohs,  1, 0);
    cc_register_cfunc(vm, "inet_addr",(void*)wrap_inet_addr, 1, 0);
    cc_register_cfunc(vm, "inet_ntoa",(void*)inet_ntoa,    1, 0);
    cc_register_cfunc(vm, "inet_ntop",(void*)inet_ntop,    4, 0);
    cc_register_cfunc(vm, "inet_pton",(void*)inet_pton,    3, 0);
    cc_register_cfunc(vm, "bzero",   (void*)wrap_bzero, 2, 0);
    cc_register_cfunc(vm, "bcopy",   (void*)wrap_bcopy, 3, 0);

    cc_register_cfunc(vm, "socket",      (void*)socket,      3, 0);
    cc_register_cfunc(vm, "socketpair",  (void*)socketpair,  4, 0);
    cc_register_cfunc(vm, "bind",        (void*)bind,        3, 0);
    cc_register_cfunc(vm, "listen",      (void*)listen,      2, 0);
    cc_register_cfunc(vm, "setsockopt",  (void*)setsockopt,  5, 0);
    cc_register_cfunc(vm, "getsockopt",  (void*)getsockopt,  5, 0);
    cc_register_cfunc(vm, "getsockname", (void*)getsockname, 3, 0);
    cc_register_cfunc(vm, "getpeername", (void*)getpeername, 3, 0);
    cc_register_cfunc(vm, "sockatmark",  (void*)sockatmark,  1, 0);
    cc_register_cfunc(vm, "shutdown",    (void*)shutdown,    2, 0);
    cc_register_cfunc(vm, "freeaddrinfo",(void*)wrap_freeaddrinfo, 1, 0);
    cc_register_cfunc(vm, "setnetent",   (void*)setnetent,      1, 0);
    cc_register_cfunc(vm, "endnetent",   (void*)endnetent,      0, 0);
    // servent/protoent (#746) -- local /etc/services /etc/protocols lookups,
    // fast and CPU-bound like getnetbyname/getnetbyaddr; no GIL release needed
    cc_register_cfunc(vm, "getservbyname",(void*)getservbyname,  2, 0);
    cc_register_cfunc(vm, "getservbyport",(void*)getservbyport, 2, 0);
    cc_register_cfunc(vm, "setservent",   (void*)setservent,    1, 0);
    cc_register_cfunc(vm, "endservent",   (void*)endservent,    0, 0);
    cc_register_cfunc(vm, "getprotobyname",  (void*)getprotobyname,   1, 0);
    cc_register_cfunc(vm, "getprotobynumber",(void*)getprotobynumber,1, 0);
    cc_register_cfunc(vm, "setprotoent",     (void*)setprotoent,     1, 0);
    cc_register_cfunc(vm, "endprotoent",     (void*)endprotoent,     0, 0);
    cc_register_cfunc(vm, "getrusage",       (void*)getrusage,       2, 0);

    // net/if.h (#788) -- interface name<->index resolution, needed to target
    // a specific interface (e.g. loopback) for IPV6_MULTICAST_IF/JOIN_GROUP
    // instead of relying on index 0. Fast/local, no GIL release needed.
    cc_register_cfunc(vm, "if_nametoindex",  (void*)if_nametoindex,  1, 0);
    cc_register_cfunc(vm, "if_indextoname",  (void*)if_indextoname,  2, 0);
    cc_register_cfunc(vm, "if_nameindex",    (void*)if_nameindex,    0, 0);
    cc_register_cfunc(vm, "if_freenameindex",(void*)if_freenameindex,1, 0);
}

#else
void register_posix_net_functions(VirtualMachine *vm) { (void)vm; }
#endif
