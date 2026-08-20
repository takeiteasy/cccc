/*
 * CCCC wrapper for sys/cdefs.h
 *
 * This stub includes Availability.h first (to set up CCCC compatibility macros)
 * then includes the real sys/cdefs.h from the system include path.
 *
 * The order matters because sys/cdefs.h uses __GNUC__ and __has_attribute
 * to define macros that CCCC cannot parse (like
 * __attribute__((__always_inline__))). By including Availability.h first, we
 * ensure __has_attribute returns 0 and __attribute__ is stripped.
 *
 * #1083: the Availability.h include below is only needed for CCCC's own
 * preprocessing (see that file's own #ifdef __CCCC__ comment) -- under
 * -c=native, a real host compiler re-lexing this same physical file (via
 * -I./include) must NOT pick up CCCC's own empty `#define __attribute__(x)`,
 * or every later __attribute__(...) in the user's own TU silently vanishes.
 * Guarded so a genuine host compiler skips straight to the real
 * sys/cdefs.h's own attribute handling instead.
 */

#ifndef _CCCC_SYS_CDEFS_H_
#define _CCCC_SYS_CDEFS_H_

#ifdef __CCCC__
/* Include CCCC's Availability.h stub first to set up compatibility macros */
#include <Availability.h>
#endif

/* Now include the real sys/cdefs.h - but we need to use include_next */
#include_next <sys/cdefs.h>

#endif /* _CCCC_SYS_CDEFS_H_ */
