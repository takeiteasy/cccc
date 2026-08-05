// Expected return: 42
// Verify that readv()/writev() release the VM GIL while blocked, just like
// read()/write()/pread()/pwrite()/preadv()/pwritev() -- unlike those, they
// were previously registered as plain (non-GIL-releasing) cfuncs, so a
// guest thread blocked in readv()/writev() stalled every other VM thread.
//
// Same shape as test_posix_gil_poll.c: thread A blocks, thread B must
// acquire the GIL to unblock it. A bare readv()/writev() would block
// forever if the GIL isn't released, so both phases use a socketpair with
// SO_RCVTIMEO/SO_SNDTIMEO to bound the block at ~1s -- a regression fails
// fast instead of hanging the test suite (which has no default per-test
// timeout). The unblocking thread usleep()s first (usleep itself releases
// the GIL) so the blocking thread is guaranteed to already be parked in
// readv()/writev() -- otherwise the test could pass by thread-ordering
// luck even with the GIL held throughout.
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

static int rv_sv[2];
static int wv_sv[2];

// --- Phase 1: readv ---

static void *readv_blocker(void *arg) {
    (void)arg;
    char buf1[2] = {0};
    char buf2[2] = {0};
    struct iovec iov[2] = {{buf1, 2}, {buf2, 2}};
    ssize_t r = readv(rv_sv[0], iov, 2);
    if (r != 4) return (void *)(long long)1;
    if (buf1[0] != 'h' || buf1[1] != 'i') return (void *)(long long)2;
    if (buf2[0] != '!' || buf2[1] != '!') return (void *)(long long)3;
    return 0;
}

static void *readv_writer(void *arg) {
    (void)arg;
    usleep(50000);
    char msg1[] = "hi";
    char msg2[] = "!!";
    struct iovec iov[2] = {{msg1, 2}, {msg2, 2}};
    writev(rv_sv[1], iov, 2);
    return 0;
}

// --- Phase 2: writev ---

#define WV_PAYLOAD_LEN 65536

static char wv_payload[WV_PAYLOAD_LEN];
static long long wv_written;

static void *writev_blocker(void *arg) {
    (void)arg;
    struct iovec iov[2] = {
        {wv_payload, WV_PAYLOAD_LEN / 2},
        {wv_payload + WV_PAYLOAD_LEN / 2, WV_PAYLOAD_LEN / 2},
    };
    wv_written = (long long)writev(wv_sv[0], iov, 2);
    return 0;
}

static void *writev_drainer(void *arg) {
    (void)arg;
    usleep(50000);
    char buf[4096];
    long long total = 0;
    while (total < WV_PAYLOAD_LEN) {
        ssize_t r = read(wv_sv[1], buf, sizeof(buf));
        if (r <= 0) break;
        total += r;
    }
    return 0;
}

int main(void) {
    // Phase 1: readv
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, rv_sv) != 0) return 10;
    struct timeval tv1 = {1, 0};
    if (setsockopt(rv_sv[0], SOL_SOCKET, SO_RCVTIMEO, &tv1, sizeof(tv1)) != 0) return 11;

    pthread_t r1, r2;
    if (pthread_create(&r1, 0, readv_blocker, 0) != 0) return 12;
    if (pthread_create(&r2, 0, readv_writer, 0) != 0) return 13;
    void *rres = 0;
    if (pthread_join(r1, &rres) != 0) return 14;
    if (pthread_join(r2, 0) != 0) return 15;
    close(rv_sv[0]);
    close(rv_sv[1]);
    if (rres != 0) return 16;

    // Phase 2: writev
    for (int i = 0; i < WV_PAYLOAD_LEN; i++) wv_payload[i] = (char)(i & 0xff);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, wv_sv) != 0) return 20;
    int small_buf = 4096;
    if (setsockopt(wv_sv[0], SOL_SOCKET, SO_SNDBUF, &small_buf, sizeof(small_buf)) != 0) return 21;
    if (setsockopt(wv_sv[1], SOL_SOCKET, SO_RCVBUF, &small_buf, sizeof(small_buf)) != 0) return 22;
    struct timeval tv2 = {1, 0};
    if (setsockopt(wv_sv[0], SOL_SOCKET, SO_SNDTIMEO, &tv2, sizeof(tv2)) != 0) return 23;

    pthread_t w1, w2;
    wv_written = -1;
    if (pthread_create(&w1, 0, writev_blocker, 0) != 0) return 24;
    if (pthread_create(&w2, 0, writev_drainer, 0) != 0) return 25;
    if (pthread_join(w1, 0) != 0) return 26;
    if (pthread_join(w2, 0) != 0) return 27;
    close(wv_sv[0]);
    close(wv_sv[1]);
    if (wv_written != WV_PAYLOAD_LEN) return 28;

    return 42;
}
