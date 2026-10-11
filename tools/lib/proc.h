#ifndef TOOLS_LIB_PROC_H
#define TOOLS_LIB_PROC_H

#include "str.h"

typedef struct {
    int    exit_code; // valid when !signaled && !timed_out
    int    signaled;  // killed by a signal other than our timeout kill
    int    signo;
    int    timed_out; // deadline hit; whole process group got SIGKILL
    StrBuf out, err;  // captured stdout / stderr
} ProcResult;

// Runs argv (PATH-searched) with stdin=/dev/null, capturing both streams.
// timeout_ms <= 0 means no deadline. Returns 0, or -1 if the spawn failed
// (errno set). Free the buffers with proc_result_free.
int proc_run(char *const argv[], int timeout_ms, ProcResult *r);
void proc_result_free(ProcResult *r);

#endif
