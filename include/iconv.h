/* iconv.h - character set conversion for CCCC
 *
 * iconv_t is an opaque handle on both hosts (a raw pointer returned by the
 * real iconv_open()), so it passes straight through as a pointer-width
 * value; (iconv_t)-1 is the error sentinel on both. iconv()'s four buffer
 * arguments are all pointers into guest memory that the host reads/writes
 * directly -- guest and host share one flat address space, so no
 * marshaling is needed.
 *
 * On macOS, linking iconv requires -liconv (verified: link fails without
 * it, succeeds with it); glibc ships iconv in libc itself, no extra link
 * flag needed.
 */

#ifndef __ICONV_H
#define __ICONV_H

#ifdef _WIN32
#error "<iconv.h> is only available on POSIX targets in CCCC"
#endif

#include "stddef.h"

typedef void *iconv_t;

extern iconv_t iconv_open(const char *tocode, const char *fromcode);
extern size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
                     char **outbuf, size_t *outbytesleft);
extern int iconv_close(iconv_t cd);

#endif /* __ICONV_H */
