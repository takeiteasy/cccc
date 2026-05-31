#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        /* child */
        _exit(42);
    }

    int status;
    pid_t r = waitpid(pid, &status, 0);
    if (r != pid) return 2;
    if (!WIFEXITED(status)) return 3;
    if (WEXITSTATUS(status) != 42) return 4;

    return 42;
}
