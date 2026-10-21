// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: sizeof\(pthread_mutex_t\)
// -m output keeps sizeof(pthread_mutex_t) symbolic.

#include <pthread.h>

extern void *malloc(unsigned long size);
extern void free(void *ptr);
extern void *memset(void *s, int c, unsigned long n);

int main(void) {
    unsigned long guest_size = sizeof(pthread_mutex_t);
    unsigned long align      = _Alignof(pthread_mutex_t);
    if (align == 0 || (align & (align - 1)) != 0)
        return 1;

    unsigned long  tail = 64;
    unsigned char *buf  = (unsigned char *)malloc(guest_size + tail);
    if (!buf)
        return 2;
    memset(buf, 0, guest_size);
    memset(buf + guest_size, 0xAA, tail);

    pthread_mutex_t *m = (pthread_mutex_t *)buf;
    if (pthread_mutex_init(m, 0) != 0) {
        free(buf);
        return 3;
    }
    if (pthread_mutex_lock(m) != 0) {
        pthread_mutex_destroy(m);
        free(buf);
        return 4;
    }
    if (pthread_mutex_unlock(m) != 0) {
        pthread_mutex_destroy(m);
        free(buf);
        return 5;
    }
    pthread_mutex_destroy(m);

    for (unsigned long i = guest_size; i < guest_size + tail; i++) {
        if (buf[i] != 0xAA) {
            free(buf);
            return 6;
        }
    }
    free(buf);

    unsigned long combined = sizeof(pthread_mutex_t) + 8;
    if (combined != guest_size + 8)
        return 7;

    return 42;
}
