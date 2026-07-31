/* sys/msg.h - SysV message queues (POSIX XSI) for CCCC
 *
 * struct msqid_ds diverges the same way as shmid_ds/semid_ds (see
 * sys/shm.h) -- macOS #pragma pack(4) vs glibc field order + reserved
 * padding -- so this declares a CCCC-canonical struct instead. msgsnd()/
 * msgrcv() take a "void *" for the message buffer (the caller defines its
 * own struct with a leading "long mtype"), so struct msgbuf here is just
 * the minimal POSIX example type, not something wrap_msgsnd/wrap_msgrcv
 * need to marshal.
 */

#ifndef __SYS_MSG_H
#define __SYS_MSG_H

#ifdef _WIN32
#error "<sys/msg.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"
#include "sys/ipc.h"
#include "unistd.h" /* for ssize_t */
#include "time.h" /* for time_t */

typedef unsigned long msgqnum_t;
typedef unsigned long msglen_t;

struct msqid_ds {
    struct ipc_perm msg_perm; /* [XSI] msg queue permissions */
    msgqnum_t msg_qnum;       /* [XSI] number of msgs on the queue */
    msglen_t msg_qbytes;      /* [XSI] max bytes on the queue */
    pid_t msg_lspid;          /* [XSI] pid of last msgsnd() */
    pid_t msg_lrpid;          /* [XSI] pid of last msgrcv() */
    time_t msg_stime;         /* [XSI] time of last msgsnd() */
    time_t msg_rtime;         /* [XSI] time of last msgrcv() */
    time_t msg_ctime;         /* [XSI] time of last msgctl() */
};

struct msgbuf {
    long mtype;
    char mtext[1];
};

#define MSG_NOERROR 010000

#ifdef __linux__
#define MSG_EXCEPT 020000
#define MSG_COPY   040000

#define MSG_STAT     11
#define MSG_INFO     12
#define MSG_STAT_ANY 13

struct msginfo {
    int msgpool;
    int msgmap;
    int msgmax;
    int msgmnb;
    int msgmni;
    int msgssz;
    int msgtql;
    unsigned short msgseg;
};
#endif

extern int msgget(key_t key, int msgflg);
extern int msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);
extern ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);
extern int msgctl(int msqid, int cmd, struct msqid_ds *buf);

#endif /* __SYS_MSG_H */
