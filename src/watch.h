// File watching for --watch. Implemented in src/watch.c on top of dmon
// (src/dmon.h); deliberately free of cccc's own types so the platform headers
// dmon pulls in (CoreServices on macOS) never meet them.
#ifndef CCCC_WATCH_H
#define CCCC_WATCH_H

#include <stdbool.h>

typedef struct {
    int    debounce_ms; // quiet time that closes a burst of events
    bool   restart;     // re-exec on a change instead of queueing it
    bool   clear;       // clear the terminal before re-exec
    char **argv;        // argv to re-exec with
    // Called on the watcher's own thread once a burst settles. Not called in
    // restart mode.
    void (*on_change)(void);
} CcWatchOptions;

bool cc_watch_init(const CcWatchOptions *opt);
bool cc_watch_active(void);

// A file, or a directory (watched recursively). Takes effect at the next
// cc_watch_arm().
void cc_watch_add(const char *path);
// A glob such as "src/*.c"; files created later that match are seen.
void cc_watch_add_glob(const char *pattern);
// Events for a matching path (full path, path relative to the working
// directory or basename) are dropped.
void cc_watch_ignore(const char *glob);
// Events for this file, or anything under this directory, are dropped.
void cc_watch_exclude(const char *path);

// Starts watching everything added since the last call.
bool cc_watch_arm(void);
int cc_watch_file_count(void);

// Blocks until a burst of changes has settled.
void cc_watch_wait(void);
// Drains the changed paths. The caller frees each string and the array.
char **cc_watch_take(int *count);

// Replaces the process with a fresh run of itself. Does not return.
void cc_watch_reexec(void);

#endif
