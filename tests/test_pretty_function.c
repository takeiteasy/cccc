// __PRETTY_FUNCTION__ is the GNU spelling of __func__ (glibc's <assert.h> uses
// it).
#include <string.h>
int main(void) {
    return strcmp(__func__, "main") == 0 && strcmp(__FUNCTION__, "main") == 0 &&
                   strcmp(__PRETTY_FUNCTION__, "main") == 0
               ? 42
               : 1;
}
