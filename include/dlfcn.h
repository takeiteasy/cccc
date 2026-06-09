/* dlfcn.h - dynamic loading declarations for POSIX CCCC targets */

#ifndef __DLFCN_H
#define __DLFCN_H

#ifdef _WIN32
#error "<dlfcn.h> is only available on POSIX targets in CCCC"
#endif

#define RTLD_LAZY 0x1
#define RTLD_NOW 0x2
#define RTLD_LOCAL 0x0
#define RTLD_GLOBAL 0x100

extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
extern char *dlerror(void);

#endif /* __DLFCN_H */
