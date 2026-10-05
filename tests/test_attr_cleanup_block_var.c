// CCCC_NATIVE_SKIP: -c=native rejects a __block variable with a cleanup attribute
//
// The cleanup of a __block variable receives the address of its storage, so it
// sees (and can change) the variable's value.

static int seen;
static void rel(int *p) {
    seen = *p;
    *p   = 0;
}

int main(void) {
    {
        __block int x __attribute__((cleanup(rel)))  = 7;
        x                                           += 35;
    }
    return seen == 42 ? 42 : 1;
}
