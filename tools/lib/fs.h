#ifndef TOOLS_LIB_FS_H
#define TOOLS_LIB_FS_H

#include <stddef.h>
#include "tool.h"

typedef struct {
    char **v;
    size_t n;
} StrList;

// Whole file as a malloc'd NUL-terminated buffer; NULL if it can't be opened.
char *fs_read_file(const char *path, size_t *len);
// Same, but exits with a diagnostic on failure.
char *fs_read_file_or_die(const char *path, size_t *len);
void fs_write_file(const char *path, const char *data, size_t len);
// Returns 1 if the file was written, 0 if it already held exactly `data`.
int fs_write_if_changed(const char *path, const char *data, size_t len);
// 1 if the file exists and holds exactly `data`.
int fs_file_equals(const char *path, const char *data, size_t len);
int fs_exists(const char *path);
int fs_is_dir(const char *path);
// Sorted matches of a glob pattern (empty list when nothing matches).
StrList fs_glob(const char *pattern);
// Sorted entry names (no "." / "..") of a directory; empty if unreadable.
StrList fs_list_dir(const char *path);
void strlist_push(StrList *l, const char *s);
void strlist_free(StrList *l);
void strlist_sort(StrList *l); // strcmp order, like Python sorted()
char *path_join(const char *a, const char *b);
// Repo root for a tool living at <root>/tools/<name>.c: pass __FILE__.
char *tool_repo_root(const char *tool_file);

#endif
