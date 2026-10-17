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

// Parallel cccc runs against a project .cccc.toml with an unapproved grant
// ask once: the first run prompts on its terminal, the runs that waited for the
// trust-list lock reuse its answer. The runs share a pseudo-terminal, started
// in order (first run's prompt seen, then the rest) so nothing depends on
// timing. The binary is $CCCC_BIN, else build/cccc, else ./cccc. Skipped when
// none exists or no pseudo-terminal can be opened.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char cccc[PATH_MAX];
static int  failed;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: " __VA_ARGS__);                             \
            fputc('\n', stderr);                                               \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

static bool find_cccc(void) {
    const char *candidates[] = {getenv("CCCC_BIN"), "build/cccc", "./cccc"};
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++)
        if (candidates[i] && realpath(candidates[i], cccc) &&
            access(cccc, X_OK) == 0)
            return true;
    return false;
}

static void write_file(const char *dir, const char *name, const char *text) {
    char p[PATH_MAX + 64];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "w");
    if (!f) {
        perror(p);
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}

static char *read_file(const char *dir, const char *name) {
    char p[PATH_MAX + 64];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    static char buf[1 << 14];
    FILE       *f = fopen(p, "r");
    size_t      n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    buf[n]        = '\0';
    if (f)
        fclose(f);
    return buf;
}

static int count(const char *hay, const char *needle) {
    int n = 0;
    for (const char *p = hay; (p = strstr(p, needle)); p += strlen(needle))
        n++;
    return n;
}

// Reads the pty master for up to `ms`, appending to buf.
static void drain(int master, char *buf, size_t size, int ms) {
    struct pollfd pfd = {.fd = master, .events = POLLIN};
    while (poll(&pfd, 1, ms) > 0 && (pfd.revents & POLLIN)) {
        size_t  len = strlen(buf);
        ssize_t n   = read(master, buf + len, size - 1 - len);
        if (n <= 0)
            break;
        buf[len + (size_t)n] = '\0';
    }
}

// One round: four runs share a terminal; `answer` is typed at the first
// prompt. Returns the number of prompts shown.
static int round_trip(const char *root, const char *answer) {
    char proj[PATH_MAX + 16], script[PATH_MAX * 3];
    snprintf(proj, sizeof proj, "%s/proj", root);
    snprintf(script, sizeof script,
             "cd '%s' && { '%s' ok.c 2>a.err & "
             "while [ ! -e go ]; do sleep 0.05; done; "
             "'%s' ok.c 2>b.err & '%s' ok.c 2>c.err & '%s' ok.c 2>d.err & "
             "wait; }",
             proj, cccc, cccc, cccc, cccc);

    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0)
        return -1;
    const char *slave = ptsname(master);
    if (!slave)
        return -1;

    pid_t pid = fork();
    if (pid == 0) {
        char xdg[PATH_MAX + 16], home[PATH_MAX + 16];
        snprintf(xdg, sizeof xdg, "%s/xdg", root);
        snprintf(home, sizeof home, "%s/home", root);
        setenv("XDG_CONFIG_HOME", xdg, 1);
        setenv("HOME", home, 1);
        unsetenv("CCCC_CONFIG_NO_PROMPT");
        unsetenv("CCCC_NO_CONFIG");
        setsid();
        int fd = open(slave, O_RDWR);
        if (fd < 0 || ioctl(fd, TIOCSCTTY, 0) != 0)
            _exit(127);
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        execl("/bin/sh", "sh", "-c", script, (char *)NULL);
        _exit(127);
    }

    static char out[1 << 14];
    out[0] = '\0';
    for (int i = 0; i < 100 && !strstr(out, "[y/N]"); i++)
        drain(master, out, sizeof out, 100);
    CHECK(strstr(out, "[y/N]"), "the first run never prompted");

    write_file(root, "proj/go", "");
    drain(master, out, sizeof out, 1500); // let the others reach the lock
    if (write(master, answer, strlen(answer)) < 0)
        failed = 1;

    int st = 0;
    for (int i = 0; i < 300; i++) {
        drain(master, out, sizeof out, 100);
        if (waitpid(pid, &st, WNOHANG) == pid)
            break;
        if (i == 299) {
            kill(pid, SIGKILL);
            waitpid(pid, &st, 0);
        }
    }
    close(master);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0,
          "the runs did not all finish (status %d)", st);
    return count(out, "Trust these settings?");
}

static void clean_state(const char *root) {
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof cmd, "rm -rf '%s/xdg' '%s/proj/go' '%s'/proj/*.err",
             root, root, root);
    if (system(cmd) != 0)
        failed = 1;
}

int main(void) {
    alarm(120);
    if (!find_cccc()) {
        printf("SKIP: trust lock (no cccc binary; set CCCC_BIN)\n");
        return 0;
    }
    char root[] = "/tmp/cccc_trust_lock_XXXXXX";
    if (!mkdtemp(root))
        return 2;
    char sub[PATH_MAX + 16];
    snprintf(sub, sizeof sub, "%s/proj", root);
    mkdir(sub, 0755);
    snprintf(sub, sizeof sub, "%s/home", root);
    mkdir(sub, 0755);
    write_file(root, "proj/ok.c", "int main(void) { return 42; }\n");
    write_file(root, "proj/.cccc.toml", "comptime-allow = [\"fs\"]\n");

    int prompts = round_trip(root, "y\n");
    if (prompts < 0) {
        printf("SKIP: trust lock (no pseudo-terminal)\n");
        goto done;
    }
    CHECK(prompts == 1, "approve: %d prompts, expected 1", prompts);
    static const char *errs[] = {"proj/a.err", "proj/b.err", "proj/c.err",
                                 "proj/d.err"};
    for (int i = 0; i < 4; i++)
        CHECK(!strstr(read_file(root, errs[i]), "ignoring"),
              "approve: %s dropped the grant", errs[i]);
    char xdg_trusted[] = "xdg/cccc/trusted";
    CHECK(count(read_file(root, xdg_trusted), "comptime-allow=fs") == 1,
          "approve: trust list should hold the grant once:\n%s",
          read_file(root, xdg_trusted));

    clean_state(root);
    prompts = round_trip(root, "n\n");
    CHECK(prompts == 1, "decline: %d prompts, expected 1", prompts);
    int warned = 0;
    for (int i = 0; i < 4; i++)
        warned += strstr(read_file(root, errs[i]), "ignoring") != NULL;
    CHECK(warned == 4, "decline: %d of 4 runs skipped the grant", warned);
    if (warned != 4)
        for (int i = 0; i < 4; i++)
            fprintf(stderr, "--- %s\n%s", errs[i], read_file(root, errs[i]));

    if (!failed)
        printf("PASS: trust lock\n");

done:;
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0)
        failed = 1;
    return failed;
}
