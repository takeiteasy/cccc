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

// kill_tree.h — killing a child and everything below it.
//
// A process group is not enough: every --build step runs in a group of its own,
// so a step that runs another build leaves that build's steps outside the
// group of the step being killed. Shared by the build driver (StepTimeout) and
// tools/testcheck.c (--timeout), which includes it by relative path so that it
// still builds with a plain `cc`.

#ifndef CCCC_KILL_TREE_H
#define CCCC_KILL_TREE_H

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Kills `root` and every descendant. Each pass stops the processes found so
// far, so nothing can fork or exit and reparent while the tree is read again;
// the kill comes once the set stops growing. The caller reaps `root`.
//
// `reap_orphans` also takes the caller's own children on Linux, where a caller
// that is a child subreaper (testcheck) inherits a descendant that daemonised.
// A build driver has sibling children of its own and must not set it.
// TODO(#2133): a descendant that daemonised before the timeout has left the
// tree; a Linux subreaper would catch it, macOS has no equivalent.
static void cc_kill_tree(pid_t root, int reap_orphans) {
    pid_t self = getpid();
    kill(root, SIGSTOP);
    pid_t *seen  = malloc(sizeof *seen);
    size_t nseen = 0, cap = 1;
    for (int pass = 0; pass < 8; pass++) {
        FILE *ps = popen("ps -A -o pid= -o ppid=", "r");
        if (!ps)
            break;
        size_t nrows = 0, rcap = 256;
        pid_t *pids  = malloc(rcap * sizeof *pids);
        pid_t *ppids = malloc(rcap * sizeof *ppids);
        long   p, pp;
        while (pids && ppids && fscanf(ps, "%ld %ld", &p, &pp) == 2) {
            if (nrows == rcap) {
                rcap  *= 2;
                pids   = realloc(pids, rcap * sizeof *pids);
                ppids  = realloc(ppids, rcap * sizeof *ppids);
                if (!pids || !ppids)
                    break;
            }
            pids[nrows]    = (pid_t)p;
            ppids[nrows++] = (pid_t)pp;
        }
        pclose(ps);
        size_t before = nseen;
        for (int grew = 1; grew && pids && ppids;) {
            grew = 0;
            for (size_t i = 0; i < nrows; i++) {
                int known = pids[i] == root || pids[i] == self;
                int child = ppids[i] == root;
#ifdef __linux__
                child |= reap_orphans && ppids[i] == self;
#else
                (void)reap_orphans;
#endif
                for (size_t k = 0; k < nseen; k++) {
                    known |= seen[k] == pids[i];
                    child |= seen[k] == ppids[i];
                }
                if (known || !child)
                    continue;
                if (nseen == cap) {
                    cap  *= 2;
                    seen  = realloc(seen, cap * sizeof *seen);
                    if (!seen)
                        break;
                }
                seen[nseen++] = pids[i];
                kill(pids[i], SIGSTOP);
                grew = 1;
            }
            if (!seen)
                break;
        }
        free(pids);
        free(ppids);
        if (!seen || nseen == before)
            break;
    }
    for (size_t i = 0; seen && i < nseen; i++)
        kill(seen[i], SIGKILL);
    free(seen);
    kill(-root, SIGKILL);
    kill(root, SIGKILL);
}

#endif // CCCC_KILL_TREE_H
