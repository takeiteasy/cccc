/* getopt.h - command-line option parsing for JCC */

#ifndef __GETOPT_H
#define __GETOPT_H

#ifdef _WIN32
#error "<getopt.h> is only available on POSIX targets in JCC"
#endif

extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

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
