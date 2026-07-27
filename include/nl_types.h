/* nl_types.h - message catalogs (POSIX) for CCCC
 *
 * nl_catd is an opaque handle on both hosts (a pointer on both macOS and
 * glibc), so it passes straight through; NL_SETD/NL_CAT_LOCALE are
 * identical (1/1) on both platforms.
 */

#ifndef __NL_TYPES_H
#define __NL_TYPES_H

#ifdef _WIN32
#error "<nl_types.h> is only available on POSIX targets in CCCC"
#endif

typedef void *nl_catd;

#define NL_SETD 1
#define NL_CAT_LOCALE 1

extern nl_catd catopen(const char *name, int oflag);
extern char *catgets(nl_catd catd, int set_id, int msg_id, const char *s);
extern int catclose(nl_catd catd);

#endif /* __NL_TYPES_H */
