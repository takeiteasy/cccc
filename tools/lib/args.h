#ifndef TOOLS_LIB_ARGS_H
#define TOOLS_LIB_ARGS_H

#include "tool.h"

typedef struct {
    const char  *name;  // "--check"
    const char  *alias; // "-j", or NULL
    int          takes_value;
    int         *flag;  // set to 1 when a boolean flag is present
    const char **value; // set for a flag that takes a value
} ArgSpec;

typedef struct {
    char **pos; // positionals, in order (points into argv)
    int    npos;
} ArgResult;

// Accepts `--x`, `--x=v`, `--x v`, `-j v` and `-jv`. An unknown flag or a
// missing value prints `usage` and exits 1; `-h`/`--help` prints it and exits
// 0.
ArgResult args_parse(int argc, char **argv, const ArgSpec *specs, int nspecs,
                     const char *usage);

#endif
