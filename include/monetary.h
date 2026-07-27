/* monetary.h - monetary value formatting (POSIX) for CCCC
 *
 * strfmon is variadic with double arguments. CCCC's variadic FFI can
 * carry doubles correctly for real (non-va_list-forwarding) call sites --
 * codegen computes double_arg_mask per call-site from the static argument
 * types (src/codegen.c), which is why a plain passthrough registration
 * works here (confirmed empirically: a "%n" conversion with a real double
 * argument round-trips correctly). This differs from vsyslog/vprintf-style
 * va_list-forwarding wrappers, where the limitation documented elsewhere
 * in this codebase actually applies.
 *
 * strfmon_l (needs locale_t) is not provided; see the locale_t and
 * per-thread-locale follow-up ticket.
 */

#ifndef __MONETARY_H
#define __MONETARY_H

#ifdef _WIN32
#error "<monetary.h> is only available on POSIX targets in CCCC"
#endif

#include "unistd.h"

extern ssize_t strfmon(char *s, size_t maxsize, const char *format, ...);

#endif /* __MONETARY_H */
