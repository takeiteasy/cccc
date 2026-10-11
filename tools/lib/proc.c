#include "proc.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void proc_result_free(ProcResult *r) {
    sb_free(&r->out);
    sb_free(&r->err);
}

static void decode_status(int status, ProcResult *r) {
    if (WIFEXITED(status)) {
        r->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        r->signaled = 1;
        r->signo    = WTERMSIG(status);
    }
}

int proc_run(char *const argv[], int timeout_ms, ProcResult *r) {
    memset(r, 0, sizeof(*r));
    int out_p[2], err_p[2];
    if (pipe(out_p) != 0)
        return -1;
    if (pipe(err_p) != 0) {
        close(out_p[0]);
        close(out_p[1]);
        return -1;
    }

    posix_spawn_file_actions_t fa;
    posix_spawnattr_t          attr;
    posix_spawn_file_actions_init(&fa);
    posix_spawnattr_init(&attr);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, out_p[1], 1);
    posix_spawn_file_actions_adddup2(&fa, err_p[1], 2);
    posix_spawn_file_actions_addclose(&fa, out_p[0]);
    posix_spawn_file_actions_addclose(&fa, err_p[0]);
    posix_spawn_file_actions_addclose(&fa, out_p[1]);
    posix_spawn_file_actions_addclose(&fa, err_p[1]);
    // own process group so a timeout can kill grandchildren too
    posix_spawnattr_setpgroup(&attr, 0);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

    pid_t pid;
    int   rc = posix_spawnp(&pid, argv[0], &fa, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    close(out_p[1]);
    close(err_p[1]);
    if (rc != 0) {
        close(out_p[0]);
        close(err_p[0]);
        errno = rc;
        return -1;
    }

    long    deadline = timeout_ms > 0 ? now_ms() + timeout_ms : -1;
    int     fds[2]   = {out_p[0], err_p[0]};
    StrBuf *bufs[2]  = {&r->out, &r->err};
    int     open_n   = 2;
    char    chunk[8192];

    while (open_n > 0 && !r->timed_out) {
        struct pollfd pfd[2];
        int           idx[2], np = 0;
        for (int i = 0; i < 2; i++)
            if (fds[i] >= 0) {
                pfd[np].fd      = fds[i];
                pfd[np].events  = POLLIN;
                pfd[np].revents = 0;
                idx[np++]       = i;
            }
        int wait_ms = -1;
        if (deadline >= 0) {
            long left = deadline - now_ms();
            if (left <= 0) {
                r->timed_out = 1;
                break;
            }
            wait_ms = (int)left;
        }
        int pr = poll(pfd, (nfds_t)np, wait_ms);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        for (int k = 0; k < np; k++) {
            if (!(pfd[k].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;
            ssize_t n = read(fds[idx[k]], chunk, sizeof(chunk));
            if (n > 0) {
                sb_append_n(bufs[idx[k]], chunk, (size_t)n);
            } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
                close(fds[idx[k]]);
                fds[idx[k]] = -1;
                open_n--;
            }
        }
    }
    for (int i = 0; i < 2; i++)
        if (fds[i] >= 0)
            close(fds[i]);

    // Pipes are drained (or abandoned on timeout); reap without blocking
    // forever on a child that closed its streams but keeps running.
    int status = 0;
    for (;;) {
        if (!r->timed_out) {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                decode_status(status, r);
                break;
            }
            if (w < 0 && errno != EINTR)
                break;
            if (deadline >= 0 && now_ms() >= deadline)
                r->timed_out = 1;
            else {
                struct timespec ts = {0, 2 * 1000 * 1000};
                nanosleep(&ts, NULL);
                continue;
            }
        }
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
        break;
    }
    return 0;
}
