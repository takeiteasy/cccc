// --watch: dmon delivers raw directory events; this file filters them against
// the watch set and debounces bursts.
#include "watch.h"

#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static void watch_log(const char *msg) {
    fprintf(stderr, "warning: watch: %s\n", msg);
}

#define DMON_IMPL
#define DMON_API_DECL     static __attribute__((unused))
#define DMON_API_IMPL     static __attribute__((unused))
#define DMON_LOG_ERROR(s) watch_log(s)
#define DMON_LOG_DEBUG(s)
#define DMON_MAX_PATH    PATH_MAX
#define DMON_MAX_WATCHES 128
#include "dmon.h"

typedef struct {
    char **v;
    int    n, cap;
} StrList;

static bool list_has(const StrList *l, const char *s) {
    for (int i = 0; i < l->n; i++)
        if (!strcmp(l->v[i], s))
            return true;
    return false;
}

static void list_add(StrList *l, const char *s) {
    if (list_has(l, s))
        return;
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v   = realloc(l->v, (size_t)l->cap * sizeof *l->v);
        if (!l->v)
            abort();
    }
    l->v[l->n++] = strdup(s);
}

typedef struct {
    StrList         files;    // exact paths
    StrList         trees;    // directories, recursive
    StrList         globs;    // absolute patterns
    StrList         ignores;  // user patterns
    StrList         excludes; // exact paths or directories
    StrList         dirs;     // directories already armed
    StrList         queue;    // changed paths since the last take
    StrList         fresh;    // directories armed by the current cc_watch_arm()
    char           *probed;   // per fresh directory: its probe event came back
    int             armed_files, armed_trees, armed_globs;
    char            cwd[PATH_MAX];

    CcWatchOptions  opt;
    pthread_mutex_t mu;
    pthread_cond_t  changed; // queue grew, or the burst settled
    struct timespec last;    // time of the newest event
    bool            settled;
    bool            notified;
    bool            active;
} Watch;

static Watch W = {.mu      = PTHREAD_MUTEX_INITIALIZER,
                  .changed = PTHREAD_COND_INITIALIZER};

// An absolute, symlink-free path, also for a file that does not exist yet.
static char *canon(const char *path) {
    char buf[PATH_MAX], dir[PATH_MAX];
    if (realpath(path, buf))
        return strdup(buf);
    const char *slash = strrchr(path, '/');
    const char *base  = slash ? slash + 1 : path;
    size_t      dlen  = slash ? (size_t)(slash - path) : 0;
    if (dlen >= sizeof dir)
        return strdup(path);
    memcpy(dir, slash ? path : ".", slash ? dlen : 1);
    dir[slash ? dlen : 1] = 0;
    if (slash && dlen == 0)
        strcpy(dir, "/");
    if (!realpath(dir, buf))
        return strdup(path);
    size_t n = strlen(buf);
    char  *r = malloc(n + strlen(base) + 2);
    memcpy(r, buf, n);
    r[n] = '/';
    strcpy(r + n + 1, base);
    return r;
}

static bool is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool under(const char *path, const char *dir) {
    size_t n = strlen(dir);
    return !strncmp(path, dir, n) && (path[n] == 0 || path[n] == '/');
}

static const char *relative_to_cwd(const char *path) {
    size_t n = strlen(W.cwd);
    return !strncmp(path, W.cwd, n) && path[n] == '/' ? path + n + 1 : path;
}

static bool pattern_matches(const char *pat, const char *path) {
    return fnmatch(pat, path, 0) == 0;
}

// Called with W.mu held.
static bool is_watched(const char *path) {
    for (int i = 0; i < W.excludes.n; i++)
        if (under(path, W.excludes.v[i]))
            return false;
    const char *base = strrchr(path, '/');
    base             = base ? base + 1 : path;
    for (int i = 0; i < W.ignores.n; i++) {
        const char *g = W.ignores.v[i];
        if (pattern_matches(g, path) ||
            pattern_matches(g, relative_to_cwd(path)) ||
            pattern_matches(g, base))
            return false;
    }
    if (list_has(&W.files, path))
        return true;
    for (int i = 0; i < W.trees.n; i++)
        if (under(path, W.trees.v[i]))
            return true;
    for (int i = 0; i < W.globs.n; i++)
        if (pattern_matches(W.globs.v[i], path))
            return true;
    return false;
}

#define PROBE_PREFIX ".cccc-watch-probe-"

static void on_event(dmon_watch_id id, dmon_action action, const char *rootdir,
                     const char *file, const char *old, void *user) {
    (void)id;
    (void)old;
    (void)user;
    // A save by rename arrives as a move onto the watched name.
    if (action == DMON_ACTION_DELETE)
        return;
    const char *base = strrchr(file, '/');
    base             = base ? base + 1 : file;
    if (!strncmp(base, PROBE_PREFIX, strlen(PROBE_PREFIX))) {
        int idx = atoi(base + strlen(PROBE_PREFIX));
        pthread_mutex_lock(&W.mu);
        if (W.probed && idx >= 0 && idx < W.fresh.n)
            W.probed[idx] = 1;
        pthread_mutex_unlock(&W.mu);
        return;
    }
    char joined[PATH_MAX * 2];
    snprintf(joined, sizeof joined, "%s/%s", rootdir, file);
    char *full = canon(joined);

    pthread_mutex_lock(&W.mu);
    if (is_watched(full)) {
        list_add(&W.queue, full);
        clock_gettime(CLOCK_MONOTONIC, &W.last);
        W.settled  = false;
        W.notified = false;
        pthread_cond_broadcast(&W.changed);
    }
    pthread_mutex_unlock(&W.mu);
    free(full);
}

static long ms_since(const struct timespec *t) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - t->tv_sec) * 1000 +
           (now.tv_nsec - t->tv_nsec) / 1000000;
}

static void sleep_ms_on_cond(long ms) {
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec  += ms / 1000;
    dl.tv_nsec += (ms % 1000) * 1000000;
    if (dl.tv_nsec >= 1000000000) {
        dl.tv_sec++;
        dl.tv_nsec -= 1000000000;
    }
    pthread_cond_timedwait(&W.changed, &W.mu, &dl);
}

static void *debounce_thread(void *arg) {
    (void)arg;
    pthread_mutex_lock(&W.mu);
    for (;;) {
        if (W.queue.n == 0 || W.notified) {
            pthread_cond_wait(&W.changed, &W.mu);
            continue;
        }
        long quiet = ms_since(&W.last);
        if (quiet < W.opt.debounce_ms) {
            sleep_ms_on_cond(W.opt.debounce_ms - quiet);
            continue;
        }
        W.settled  = true;
        W.notified = true;
        pthread_cond_broadcast(&W.changed);
        if (W.opt.restart) {
            pthread_mutex_unlock(&W.mu);
            cc_watch_reexec();
        }
        if (W.opt.on_change) {
            pthread_mutex_unlock(&W.mu);
            W.opt.on_change();
            pthread_mutex_lock(&W.mu);
        }
    }
    return NULL;
}

bool cc_watch_init(const CcWatchOptions *opt) {
    if (W.active)
        return true;
    W.opt = *opt;
    if (W.opt.debounce_ms <= 0)
        W.opt.debounce_ms = 100;
    char *cwd = getcwd(W.cwd, sizeof W.cwd) ? canon(W.cwd) : NULL;
    if (cwd) {
        snprintf(W.cwd, sizeof W.cwd, "%s", cwd);
        free(cwd);
    }
    dmon_init();
    pthread_t th;
    if (pthread_create(&th, NULL, debounce_thread, NULL) != 0)
        return false;
    pthread_detach(th);
    W.active = true;
    return true;
}

bool cc_watch_active(void) {
    return W.active;
}

void cc_watch_add(const char *path) {
    char *p = canon(path);
    pthread_mutex_lock(&W.mu);
    list_add(is_dir(p) ? &W.trees : &W.files, p);
    pthread_mutex_unlock(&W.mu);
    free(p);
}

void cc_watch_add_glob(const char *pattern) {
    while (!strncmp(pattern, "./", 2))
        pattern += 2;
    char abs[PATH_MAX * 2];
    if (pattern[0] == '/')
        snprintf(abs, sizeof abs, "%s", pattern);
    else
        snprintf(abs, sizeof abs, "%s/%s", W.cwd, pattern);
    pthread_mutex_lock(&W.mu);
    list_add(&W.globs, abs);
    pthread_mutex_unlock(&W.mu);
}

void cc_watch_ignore(const char *glob) {
    pthread_mutex_lock(&W.mu);
    list_add(&W.ignores, glob);
    pthread_mutex_unlock(&W.mu);
}

void cc_watch_exclude(const char *path) {
    char *p = canon(path);
    pthread_mutex_lock(&W.mu);
    list_add(&W.excludes, p);
    pthread_mutex_unlock(&W.mu);
    free(p);
}

typedef struct {
    char *dir;
    bool  recursive;
} Arm;

// Called with W.mu held. dmon itself is only called after the lock is
// released: its thread holds its own mutex while it calls on_event, which takes
// W.mu.
static void queue_arm(Arm **todo, int *n, const char *dir, bool recursive) {
    if (list_has(&W.dirs, dir) || !is_dir(dir))
        return;
    list_add(&W.dirs, dir);
    list_add(&W.fresh, dir);
    *todo       = realloc(*todo, (size_t)(*n + 1) * sizeof **todo);
    (*todo)[*n] = (Arm){strdup(dir), recursive};
    (*n)++;
}

// FSEvents starts delivering a moment after the stream is created, and an
// edit made inside that gap is lost. Touch a probe file in each new directory
// until its event comes back. inotify watches are live as soon as they are
// added.
static void await_streams(void) {
#ifdef __APPLE__
    pthread_mutex_lock(&W.mu);
    int n       = W.fresh.n;
    W.probed    = calloc((size_t)(n ? n : 1), 1);
    char **dirs = malloc((size_t)(n ? n : 1) * sizeof *dirs);
    for (int i = 0; i < n; i++)
        dirs[i] = strdup(W.fresh.v[i]);
    pthread_mutex_unlock(&W.mu);

    for (int round = 0; round < 80; round++) {
        bool all = true;
        for (int i = 0; i < n; i++) {
            pthread_mutex_lock(&W.mu);
            bool seen = W.probed[i];
            pthread_mutex_unlock(&W.mu);
            if (seen)
                continue;
            all = false;
            char probe[PATH_MAX * 2];
            snprintf(probe, sizeof probe, "%s/" PROBE_PREFIX "%d", dirs[i], i);
            FILE *f = fopen(probe, "w");
            if (f) {
                fputc('x', f);
                fclose(f);
            }
        }
        if (all)
            break;
        usleep(25000);
    }
    // Let the probes' last events land, then remove them.
    usleep(50000);
    for (int i = 0; i < n; i++) {
        char probe[PATH_MAX * 2];
        snprintf(probe, sizeof probe, "%s/" PROBE_PREFIX "%d", dirs[i], i);
        unlink(probe);
        free(dirs[i]);
    }
    free(dirs);
    pthread_mutex_lock(&W.mu);
    free(W.probed);
    W.probed = NULL;
    pthread_mutex_unlock(&W.mu);
#endif
}

bool cc_watch_arm(void) {
    bool ok     = true;
    Arm *todo   = NULL;
    int  todo_n = 0;
    pthread_mutex_lock(&W.mu);
    for (int i = 0; i < W.fresh.n; i++)
        free(W.fresh.v[i]);
    W.fresh.n = 0;
    for (; W.armed_files < W.files.n; W.armed_files++) {
        char *dir = canon(W.files.v[W.armed_files]);
        char *cut = strrchr(dir, '/');
        if (cut > dir)
            *cut = 0;
        else if (cut)
            cut[1] = 0;
        queue_arm(&todo, &todo_n, dir, false);
        free(dir);
    }
    for (; W.armed_trees < W.trees.n; W.armed_trees++)
        queue_arm(&todo, &todo_n, W.trees.v[W.armed_trees], true);
    for (; W.armed_globs < W.globs.n; W.armed_globs++) {
        char       *pat = W.globs.v[W.armed_globs];
        const char *wc  = strpbrk(pat, "*?[");
        size_t      end = wc ? (size_t)(wc - pat) : strlen(pat);
        char        base[PATH_MAX * 2];
        memcpy(base, pat, end);
        base[end]       = 0;
        char *cut       = strrchr(base, '/');
        bool  recursive = wc && strchr(wc, '/');
        if (cut > base)
            *cut = 0;
        else if (cut)
            cut[1] = 0;
        char *dir = canon(base);
        queue_arm(&todo, &todo_n, dir, recursive);
        free(dir);
    }
    pthread_mutex_unlock(&W.mu);
    for (int i = 0; i < todo_n; i++) {
        dmon_watch_id id =
            dmon_watch(todo[i].dir, on_event,
                       todo[i].recursive ? DMON_WATCHFLAGS_RECURSIVE : 0, NULL);
        ok &= id.id != 0;
        free(todo[i].dir);
    }
    free(todo);
    await_streams();
    return ok;
}

int cc_watch_file_count(void) {
    pthread_mutex_lock(&W.mu);
    int n = W.files.n;
    pthread_mutex_unlock(&W.mu);
    return n;
}

void cc_watch_wait(void) {
    pthread_mutex_lock(&W.mu);
    while (!W.settled)
        pthread_cond_wait(&W.changed, &W.mu);
    pthread_mutex_unlock(&W.mu);
}

char **cc_watch_take(int *count) {
    pthread_mutex_lock(&W.mu);
    char **v = W.queue.v;
    *count   = W.queue.n;
    memset(&W.queue, 0, sizeof W.queue);
    W.settled  = false;
    W.notified = false;
    pthread_cond_broadcast(&W.changed);
    pthread_mutex_unlock(&W.mu);
    return v;
}

static bool self_path(char *out, size_t cap) {
#ifdef __APPLE__
    (void)cap;
    char     raw[PATH_MAX];
    uint32_t n = sizeof raw;
    if (_NSGetExecutablePath(raw, &n) != 0)
        return false;
    return realpath(raw, out) != NULL;
#else
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n < 0)
        return false;
    out[n] = 0;
    return true;
#endif
}

void cc_watch_reexec(void) {
    char self[PATH_MAX];
    if (!self_path(self, sizeof self)) {
        fprintf(stderr, "error: watch: cannot find the cccc executable\n");
        _exit(1);
    }
    fflush(NULL);
    if (W.opt.clear && isatty(STDOUT_FILENO))
        (void)!write(STDOUT_FILENO, "\033[H\033[2J", 7);
    execv(self, W.opt.argv);
    fprintf(stderr, "error: watch: cannot restart %s: %s\n", self,
            strerror(errno));
    _exit(1);
}
