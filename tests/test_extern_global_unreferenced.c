// #957: a declared-but-never-referenced extern global (ordinary or TLS)
// must still compile cleanly -- the undefined-global check only fires for
// globals codegen actually materialized an address for (gen_addr), not
// merely declared.
extern int never_used;
extern __thread int tls_never_used;

int main(void) {
    return 42;
}
