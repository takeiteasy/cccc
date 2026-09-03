/* xlocale.h - locale-explicit ("_l" suffix) API surface for CCCC
 *
 * Real macOS/BSD hosts split the per-object-locale API (locale_t plus the
 * is*_l/to*_l/nl_langinfo_l/strfmon_l family) out of <locale.h>/<ctype.h>/
 * <langinfo.h>/<monetary.h> into this separate header, included from those
 * (xlocale/_ctype.h, xlocale/_langinfo.h, xlocale/_monetary.h). CCCC does
 * not replicate that split -- every declaration real <xlocale.h> would pull
 * in already lives directly in CCCC's own locale.h (locale_t, LC_*_MASK,
 * newlocale/duplocale/freelocale/uselocale) and ctype.h (the is*_l/to*_l
 * family). This header exists purely so `#include <xlocale.h>` -- written
 * directly by guest code, or reached via src/stdlib/ctype.c's own
 * `#ifdef __APPLE__ / #include <xlocale.h>` used to register those
 * functions' addresses with the FFI -- resolves to something, portably on
 * every host CCCC supports.
 *
 * Before this file existed, that angle-include fell through to the real
 * SDK's own <xlocale.h> -> <_locale.h>, whose `struct lconv` collided with
 * CCCC's own trimmed one from locale.h in the same TU -- reproducible only
 * once cccc's own source (src/stdlib/ctype.c) was itself compiled under
 * -c=native/-c=generated (#1132's self-hosting spike), filed and root-caused
 * as #1275.
 */

#ifndef __XLOCALE_H
#define __XLOCALE_H

#ifdef __CCCC__

#include <ctype.h>  /* is*_l/to*_l -- #1070: angle-bracket for the correct
                        #include_next hand-off under real GCC */
#include <locale.h> /* locale_t, LC_*_MASK, newlocale/duplocale/uselocale --
                        same #1070 reason */

#else
/* A real host compiler is reprocessing this file -- only possible during
 * -c=native/-c=generated serializer replay (see the file comment above).
 * Neither Makefile nor build.c puts ./include on the host cc's own search
 * path, so this branch only fires if some other -I does; it's defensive,
 * not load-bearing for the ordinary build. Guarded on __has_include_next
 * (the same pattern Availability.h already uses) because glibc dropped its
 * own <xlocale.h> in 2.26 -- an unconditional #include_next would hard-error
 * there. serialize_program.c's own emit-directives loop additionally never
 * replays a captured `#include <xlocale.h>` line outside __APPLE__, so a
 * native/generated replay on Linux never reaches this branch at all; it
 * exists only for symmetry with locale.h/pthread.h/fenv.h's own hand-off
 * shape.
 */
#ifdef __has_include_next
#if __has_include_next(<xlocale.h>)
#include_next <xlocale.h>
#endif
#endif

#endif /* __CCCC__ */

#endif /* __XLOCALE_H */
