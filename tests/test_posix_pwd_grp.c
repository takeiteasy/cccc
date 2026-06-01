#include <pwd.h>
#include <grp.h>
#include <unistd.h>

int main(void) {
    struct passwd *pw = getpwuid(0);
    if (!pw) return 1;
    if (!pw->pw_name || !pw->pw_dir || !pw->pw_shell) return 2;

    struct passwd *pw2 = getpwnam(pw->pw_name);
    if (!pw2) return 3;
    if (pw2->pw_uid != pw->pw_uid) return 4;

    struct group *gr = getgrgid(0);
    if (!gr) return 5;
    if (!gr->gr_name) return 6;

    struct group *gr2 = getgrnam(gr->gr_name);
    if (!gr2) return 7;
    if (gr2->gr_gid != gr->gr_gid) return 8;

    return 42;
}
