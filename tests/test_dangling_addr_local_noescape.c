// CCCC_FLAGS: -3
// Ticket #669: at safety tier -3, the SCOPEOUT dangling-pointer check used
// to hard-abort on *any* `&local` still tracked at scope exit, regardless
// of whether the pointer actually escaped. MARKA tags every address-of-local
// with the enclosing function's own scope_id, so the check only ever fired
// at function exit -- where the frame (and the local) dies together and
// nothing can dangle. A plain, non-escaping local pointer like this used to
// abort with "DANGLING POINTER DETECTED"; it must now run to completion.
int main(void) {
    int  n = 0;
    int *p = &n;
    *p     = 5;
    return n == 5 ? 42 : 1;
}
