/*
 CCCC: Comprehensiev C Compensation Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// wait_stop.h — waiting on a child that may stop itself.
//
// Under macOS `leaks -atExit` a process stops itself at exit so the tool can
// walk its heap, and that instrumentation is inherited by every forked child
// and cleared only by exec. A child of cccc's own process therefore stops for
// a tool that is only ever going to attach to the parent, and never resumes:
// `exit` and `_exit` stop alike, so the exit path cannot avoid it. Every wait
// in cccc goes through cc_wait_resume(), which continues a stopped child and
// keeps waiting for it to finish.

#ifndef CCCC_WAIT_STOP_H
#define CCCC_WAIT_STOP_H

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// waitpid() that reports only a finished child: EINTR is retried, and a child
// that stopped is continued and waited for again. `pid` may be -1. Returns 0
// with WNOHANG while no child has finished, or -1 with errno set.
static inline pid_t cc_wait_resume(pid_t pid, int *status, int flags) {
    for (;;) {
        int   st = 0;
        pid_t r  = waitpid(pid, &st, flags | WUNTRACED);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return r;
        }
        if (r == 0)
            return 0; // WNOHANG, nothing finished
        if (WIFSTOPPED(st)) {
            kill(r, SIGCONT);
            continue;
        }
        if (status)
            *status = st;
        return r;
    }
}

#endif // CCCC_WAIT_STOP_H
