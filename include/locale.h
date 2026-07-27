/* locale.h - localization declarations for CCCC
 *
 * LC_* numbering is CCCC's own canonical numbering, not the host's --
 * macOS and glibc disagree entirely (macOS: ALL=0/COLLATE=1/CTYPE=2/
 * MONETARY=3/NUMERIC=4/TIME=5; glibc: CTYPE=0/NUMERIC=1/TIME=2/COLLATE=3/
 * MONETARY=4/MESSAGES=5/ALL=6). wrap_setlocale (src/stdlib/locale.c)
 * translates this canonical numbering to the host's real values before
 * calling the host setlocale(), the same pattern used for _SC_, _PC_,
 * and _CS_ constants in src/stdlib/posix.c -- this keeps compiled .c4
 * bytecode portable across hosts.
 *
 * The POSIX.1-2008 per-thread locale API (locale_t + newlocale/duplocale/
 * freelocale/uselocale) uses its own LC_*_MASK bitmask numbering, distinct
 * from the plain LC_* category numbers above -- deliberately so, since
 * canonical LC_ALL == 0 and "1 << LC_ALL" would collide with
 * LC_COLLATE_MASK. The bits below copy macOS's own assignment, so macOS
 * needs no translation; guest_to_host_lc_mask (src/stdlib/locale.c)
 * translates to the host's real LC_*_MASK values on Linux. LC_ALL_MASK is
 * special-cased there too: glibc's real LC_ALL_MASK ORs 12 categories
 * (it also covers PAPER/NAME/ADDRESS/TELEPHONE/MEASUREMENT/
 * IDENTIFICATION), not just the 6 CCCC exposes, so a bit-by-bit map of the
 * canonical LC_ALL_MASK would build a partially-unset locale on Linux.
 *
 * locale_t itself is an opaque handle on both hosts (a raw pointer), the
 * same pattern already used for iconv_t (include/iconv.h) and nl_catd
 * (include/nl_types.h), so it passes straight through as a pointer-width
 * value with no marshaling.
 *
 * freelocale()'s return type diverges between hosts: macOS returns int,
 * glibc (POSIX) returns void. wrap_freelocale (src/stdlib/locale.c)
 * normalizes both to a void guest-visible signature.
 */

#ifndef __LOCALE_H
#define __LOCALE_H

#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MONETARY 3
#define LC_NUMERIC 4
#define LC_TIME 5
#define LC_MESSAGES 6

/* LC_*_MASK bits for newlocale()/uselocale() -- see the file comment above
   for why these use a different (bitmask) numbering than the plain LC_*
   category constants. */
#define LC_COLLATE_MASK  (1 << 0)
#define LC_CTYPE_MASK    (1 << 1)
#define LC_MESSAGES_MASK (1 << 2)
#define LC_MONETARY_MASK (1 << 3)
#define LC_NUMERIC_MASK  (1 << 4)
#define LC_TIME_MASK     (1 << 5)
#define LC_ALL_MASK      0x3f

typedef void *locale_t;

/* Passing this value to any function other than uselocale() is undefined
   behavior. */
#define LC_GLOBAL_LOCALE ((locale_t)-1)

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char int_frac_digits;
    char frac_digits;
    char p_cs_precedes;
    char p_sep_by_space;
    char n_cs_precedes;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
};

extern char *setlocale(int category, const char *locale);
extern struct lconv *localeconv(void);

extern locale_t newlocale(int category_mask, const char *locale, locale_t base);
extern locale_t duplocale(locale_t locobj);
extern void freelocale(locale_t locobj);
extern locale_t uselocale(locale_t newloc);

#endif /* __LOCALE_H */
