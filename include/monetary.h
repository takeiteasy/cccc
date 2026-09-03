/* monetary.h - monetary value formatting (POSIX) for CCCC
 *
 * strfmon is variadic with double arguments. CCCC's variadic FFI can
 * carry doubles correctly for real (non-va_list-forwarding) call sites --
 * codegen computes double_arg_mask per call-site from the static argument
 * types (src/codegen.c), which is why a plain passthrough registration
 * works here (confirmed empirically: a "%n" conversion with a real double
 * argument round-trips correctly). This differs from vsyslog/vprintf-style
 * va_list-forwarding wrappers, where the limitation documented elsewhere
 * in this codebase actually applies. strfmon_l follows the same variadic
 * registration, just with a locale_t (see locale.h) ahead of the format
 * string.
 *
 * On macOS, the host strfmon() itself has an internal scratch-buffer
 * over-read that only AddressSanitizer notices (not a CCCC bug -- confirmed
 * with a standalone clang -fsanitize=address program with no CCCC involved).
 * ASan builds carry a built-in suppression for it; see the
 * __asan_default_suppressions hook in src/stdlib/posix.c (#841).
 */

#ifndef __MONETARY_H
#define __MONETARY_H

#ifdef _WIN32
#error "<monetary.h> is only available on POSIX targets in CCCC"
#endif

#include "unistd.h"
#include <locale.h> /* #1070: angle-bracket for the locale.h #include_next handoff */

extern ssize_t strfmon(char *s, size_t maxsize, const char *format, ...);
extern ssize_t strfmon_l(char *s, size_t maxsize, locale_t loc,
                         const char *format, ...);

#endif /* __MONETARY_H */
