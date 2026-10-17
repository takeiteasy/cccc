// Without --allow-url the probes answer from the cache and never download.
#if __has_include("http://127.0.0.1:1/no_such_header.h")
#error "an uncached URL must not be reported as present"
#endif

#if __has_embed("http://127.0.0.1:1/no_such_data.bin") != 0
#error "an uncached URL embed must not be reported as present"
#endif

int main(void) {
    return 42;
}
