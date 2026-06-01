#include <dirent.h>
#include <string.h>

int main(void) {
    DIR *d = opendir("tests");
    if (!d) return 1;

    int found = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != 0) {
        if (strcmp(ent->d_name, "test_posix_dirent.c") == 0) {
            found = 1;
            if (ent->d_reclen == 0) return 2;
            break;
        }
    }

    if (closedir(d) != 0) return 3;
    if (!found) return 4;
    return 42;
}
