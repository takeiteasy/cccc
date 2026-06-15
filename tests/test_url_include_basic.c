// Test basic URL include functionality
// This test is only compiled when JCC_HAS_CURL is enabled

#ifdef JCC_HAS_CURL

// Test including a header from a URL
// For testing purposes, we'll use a simple header from a known stable URL
// In real usage, this could be any valid C header file

#include <https://raw.githubusercontent.com/nothings/stb/master/stb_sprintf.h>

int main() {
    // If the include worked, stb_sprintf should be available
    char buffer[100];
    stbsp_sprintf(buffer, "Hello from URL include!");

    return 42;
}

#else

// Fallback test when curl is not available
int main() {
    return 42;
}

#endif
