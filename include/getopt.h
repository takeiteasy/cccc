/* getopt.h - command-line option parsing for CCCC */

#ifndef __GETOPT_H
#define __GETOPT_H

#ifdef _WIN32
#error "<getopt.h> is only available on POSIX targets in CCCC"
#endif

/* #1040: same header-shadow trap as include/stdio.h's __cccc_stdin/etc -- */
/* see that header's comment for the full reasoning (a bare extern-only */
/* guard still leaves `#define optarg (*__cccc_optarg_ptr())` live, which */
/* would loop the shim body back into itself once a real host compiler */
/* re-lexes this same physical file during -c=native/-c=generated replay). */
/* Whole CCCC-flavored body guarded on __CCCC__, handing off to the host's */
/* own <getopt.h> via #include_next otherwise. */
#ifdef __CCCC__

/* These alias the host's real getopt() state (via accessor functions, same
 * pattern as stdin/stdout/stderr in stdio.h) so they reflect what the host's
 * getopt()/getopt_long() actually parsed instead of being inert, always-zero
 * guest globals (#736). */
extern char **__cccc_optarg_ptr(void);
extern int *__cccc_optind_ptr(void);
extern int *__cccc_opterr_ptr(void);
extern int *__cccc_optopt_ptr(void);
#define optarg            (*__cccc_optarg_ptr())
#define optind            (*__cccc_optind_ptr())
#define opterr            (*__cccc_opterr_ptr())
#define optopt            (*__cccc_optopt_ptr())

#define no_argument       0
#define required_argument 1
#define optional_argument 2

struct option {
    const char *name;
    int         has_arg;
    int        *flag;
    int         val;
};

extern int getopt(int argc, char *const argv[], const char *optstring);
extern int getopt_long(int argc, char *const argv[], const char *optstring,
                       const struct option *longopts, int *longindex);

/* #1282: optreset -- BSD/Darwin-only (no glibc equivalent; glibc's getopt()
 * restarts a scan on its own the moment it notices optind went backwards,
 * see src/vm.c's cccc_reset_getopt_state() for the full explanation this
 * mirrors). Same accessor-macro shape as optarg/optind/opterr/optopt above,
 * same #1040/#736 reasoning. */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) ||      \
    defined(__NetBSD__)
extern int *__cccc_optreset_ptr(void);
#define optreset (*__cccc_optreset_ptr())
#endif

#else
#include_next <getopt.h>
#endif /* __CCCC__ */

#endif /* __GETOPT_H */
