// Minimal bytecode-LinkWith fixture: executable entry point.
// Calls a function defined in a separate library target (#563).
#include "answer.h"

int main(void) {
    return answer();
}
