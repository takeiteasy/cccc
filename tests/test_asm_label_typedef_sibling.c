// An asm label applies to its own declarator only, not to later declarations
// that share the base type.
extern int labelled __asm__("labelled_sym");
typedef int T;
extern T other __asm__("other_sym");
extern T plain;
int plain = 4;

int main(void) {
    return plain + 38;
}
