// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: allow-url|CCCC_HAS_CURL
// A URL include that is not cached needs --allow-url (or a curl-less build
// says so); the port is closed, so nothing could be fetched anyway.
#include "http://127.0.0.1:1/no_such_header.h"

int main(void) {
    return 42;
}
