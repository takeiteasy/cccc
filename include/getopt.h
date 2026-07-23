/* getopt.h - command-line option parsing for CCCC */

#ifndef __GETOPT_H
#define __GETOPT_H

#ifdef _WIN32
#error "<getopt.h> is only available on POSIX targets in CCCC"
#endif

/* These alias the host's real getopt() state (via accessor functions, same
 * pattern as stdin/stdout/stderr in stdio.h) so they reflect what the host's
 * getopt()/getopt_long() actually parsed instead of being inert, always-zero
 * guest globals (#736). */
extern char **__cccc_optarg_ptr(void);
extern int *__cccc_optind_ptr(void);
extern int *__cccc_opterr_ptr(void);
extern int *__cccc_optopt_ptr(void);
#define optarg (*__cccc_optarg_ptr())
#define optind (*__cccc_optind_ptr())
#define opterr (*__cccc_opterr_ptr())
#define optopt (*__cccc_optopt_ptr())

#define no_argument       0
#define required_argument 1
#define optional_argument 2

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

extern int getopt(int argc, char *const argv[], const char *optstring);
extern int getopt_long(int argc, char *const argv[], const char *optstring,
                       const struct option *longopts, int *longindex);

#endif /* __GETOPT_H */
