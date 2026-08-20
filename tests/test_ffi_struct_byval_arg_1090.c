// Expected return: 42
// #1090 (found auditing #1087): a raw FFI registration of a host function
// whose C ABI takes a struct/union *by value* is wrong -- CCCC always
// passes a guest struct/union argument as a pointer to the caller's own
// storage (#714/#1078/#1085), never a scalar copy of the struct's own
// member(s). inet_ntoa(struct in_addr in) was registered raw
// (src/stdlib/posix_net.c), so the guest's pointer landed in the host's
// by-value slot and the low 32 bits of that pointer were read back as
// s_addr -- confirmed failing pre-fix (printed a garbage address instead
// of 127.0.0.1). Fixed with wrap_inet_ntoa, which copies the guest struct
// out of the pointer before calling the real host inet_ntoa.
//
// hsearch(ENTRY item, ACTION) is the *other* by-value-struct FFI entry
// point (src/stdlib/posix_search.c) -- included here as a control that was
// already correct (wrap_hsearch already copies), confirming #1087's own
// audit answer: no second instance of the mutation class exists.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <search.h>
#include <string.h>

int main(void) {
    struct in_addr a;
    a.s_addr      = htonl(0x7f000001); // 127.0.0.1
    const char *s = inet_ntoa(a);
    if (!s || strcmp(s, "127.0.0.1") != 0)
        return 1;

    // A second call with a different address must not be corrupted by the
    // first's argument marshalling (no shared/aliased scratch storage).
    struct in_addr b;
    b.s_addr       = htonl(0xc0a80101); // 192.168.1.1
    const char *s2 = inet_ntoa(b);
    if (!s2 || strcmp(s2, "192.168.1.1") != 0)
        return 2;

    // hsearch control: by-value ENTRY, already correct pre-#1090.
    if (hcreate(16) == 0)
        return 3;
    ENTRY  e = {.key = "k", .data = (void *)"v"};
    ENTRY *r = hsearch(e, ENTER);
    if (!r || strcmp((const char *)r->data, "v") != 0)
        return 4;
    ENTRY  find  = {.key = "k", .data = NULL};
    ENTRY *found = hsearch(find, FIND);
    if (!found || strcmp((const char *)found->data, "v") != 0)
        return 5;
    // Deliberately no hdestroy() here: confirmed directly (both under CCCC
    // and with a real host `cc` on the same platform) that hdestroy()
    // aborts (SIGABRT) after entering a string-literal key -- a real host
    // libc footgun (macOS's hsearch/hdestroy implementation), unrelated to
    // #1090/#1087 and not exercised by this test.

    return 42;
}
