// --use-system-headers suppresses "unknown pragma" noise from SDK-style
// pragmas. Pragmas like #pragma clang assume_nonnull and unknown #pragma GCC
// system_header are common in SDK headers; they must not produce warnings in
// this mode.
// CCCC_FLAGS: --use-system-headers
// CCCC_REJECT_STDERR: unknown pragma|unknown warning option
#pragma clang assume_nonnull begin
#pragma GCC system_header
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wno-such-warning-this-is-fake"
#pragma clang diagnostic pop
int main(void) {
    return 42;
}
