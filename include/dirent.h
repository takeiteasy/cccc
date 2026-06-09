/* dirent.h - directory entry iteration for CCCC */

#ifndef __DIRENT_H
#define __DIRENT_H

#ifdef _WIN32
#error "<dirent.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"

typedef struct __cccc_DIR DIR;

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12
#define DT_WHT     14

#ifdef __APPLE__
struct dirent {
    ino_t d_ino;
    unsigned long long d_seekoff;
    unsigned short d_reclen;
    unsigned short d_namlen;
    unsigned char d_type;
    char d_name[1024];
};
#else
struct dirent {
    ino_t d_ino;
    off_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};
#endif

extern DIR *opendir(const char *dirname);
extern struct dirent *readdir(DIR *dirp);
extern int closedir(DIR *dirp);

#endif /* __DIRENT_H */
