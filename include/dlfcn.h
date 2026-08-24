/* dlfcn.h - dynamic loading declarations for POSIX CCCC targets */

#ifndef __DLFCN_H
#define __DLFCN_H

#ifdef _WIN32
#error "<dlfcn.h> is only available on POSIX targets in CCCC"
#endif

/* #1152: see include/errno.h's matching comment -- this exact file is also
 * what a native/generated re-emission's replayed `#include <dlfcn.h>`
 * resolves to when a caller passes `-I` at CCCC's own bundled include dir
 * (tools/testing/native.py does exactly this; see man/HEADERS.md's "Passing
 * -I at CCCC's own bundled headers" section), but a real host compiler
 * reprocessing the CCCC-flavored content below has never heard of the
 * __CCCC_RTLD_*__ macros init_dlfcn_macros() (src/preprocess.c) injects.
 * __CCCC__ is absent only when a genuine host compiler is reading this
 * physical file, which only happens during serializer replay -- hand off to
 * #include_next for the host's own, self-contained <dlfcn.h> in that case.
 * The RTLD_* values below are already baked into the AST as plain integer
 * literals by the time any of this runs (CCCC's own guest-side parsing
 * constant-folds every use), so skipping their (re)definition here costs
 * nothing.
 * Spelled `#ifdef __CCCC__` (not `#if !defined(__CCCC__) ... #else`) so
 * tools/audit_ffi.py's guard-presence check sees a plain, nameable
 * condition it can whitelist (GUEST_ONLY_DECL_GUARDS), the same way it
 * already does for errno.h. */
#ifdef __CCCC__

/* RTLD_LAZY/RTLD_NOW/RTLD_LOCAL/RTLD_GLOBAL, and any of RTLD_NOLOAD/
 * RTLD_NODELETE/RTLD_FIRST/RTLD_DEEPBIND/RTLD_BINDING_MASK the host libdl
 * has, are derived from the real host <dlfcn.h> this cccc binary was built
 * against (init_dlfcn_macros(), src/preprocess.c) rather than hand-
 * transcribed here -- #1152 was exactly a hand-transcription bug: glibc's
 * RTLD_GLOBAL (0x100) was used unconditionally, but 0x100 is RTLD_FIRST on
 * macOS, a different flag entirely. A flag the host libdl doesn't have is
 * simply never injected, so using it here is a compile error rather than a
 * silently wrong integer.
 *
 * NOT provided: the dlsym() pseudo-handles (RTLD_NEXT/RTLD_DEFAULT/
 * RTLD_SELF/RTLD_MAIN_ONLY). cccc_rt_dlsym (src/vm.c) resolves its handle
 * argument through the VM's own dynamic-library registry, not a raw host
 * handle, so a pseudo-handle would behave differently between the VM and
 * -c=native -- see man/COVERAGE.md's <dlfcn.h> entry. */
#define RTLD_LAZY   __CCCC_RTLD_LAZY__
#define RTLD_NOW    __CCCC_RTLD_NOW__
#define RTLD_LOCAL  __CCCC_RTLD_LOCAL__
#define RTLD_GLOBAL __CCCC_RTLD_GLOBAL__
#ifdef __CCCC_RTLD_NOLOAD__
#define RTLD_NOLOAD __CCCC_RTLD_NOLOAD__
#endif
#ifdef __CCCC_RTLD_NODELETE__
#define RTLD_NODELETE __CCCC_RTLD_NODELETE__
#endif
#ifdef __CCCC_RTLD_FIRST__
#define RTLD_FIRST __CCCC_RTLD_FIRST__
#endif
#ifdef __CCCC_RTLD_DEEPBIND__
#define RTLD_DEEPBIND __CCCC_RTLD_DEEPBIND__
#endif
#ifdef __CCCC_RTLD_BINDING_MASK__
#define RTLD_BINDING_MASK __CCCC_RTLD_BINDING_MASK__
#endif

extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
extern char *dlerror(void);

#else /* !__CCCC__ */

#include_next <dlfcn.h>

#endif /* __CCCC__ */

#endif /* __DLFCN_H */
