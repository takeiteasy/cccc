/* ctype.h - character classification for CCCC C compiler
 *
 * The _l family (POSIX.1-2008) takes an explicit locale_t instead of
 * consulting the process-global/per-thread locale; see locale.h for
 * locale_t and the newlocale()/uselocale() family that produces one.
 */

#ifndef __CTYPE_H
#define __CTYPE_H

#include "locale.h"

extern int isalnum(int c);
extern int isalpha(int c);
extern int isblank(int c);
extern int iscntrl(int c);
extern int isdigit(int c);
extern int isgraph(int c);
extern int islower(int c);
extern int isprint(int c);
extern int ispunct(int c);
extern int isspace(int c);
extern int isupper(int c);
extern int isxdigit(int c);

extern int toupper(int c);
extern int tolower(int c);

extern int isalnum_l(int c, locale_t loc);
extern int isalpha_l(int c, locale_t loc);
extern int isblank_l(int c, locale_t loc);
extern int iscntrl_l(int c, locale_t loc);
extern int isdigit_l(int c, locale_t loc);
extern int isgraph_l(int c, locale_t loc);
extern int islower_l(int c, locale_t loc);
extern int isprint_l(int c, locale_t loc);
extern int ispunct_l(int c, locale_t loc);
extern int isspace_l(int c, locale_t loc);
extern int isupper_l(int c, locale_t loc);
extern int isxdigit_l(int c, locale_t loc);

extern int toupper_l(int c, locale_t loc);
extern int tolower_l(int c, locale_t loc);

#endif /* __CTYPE_H */
