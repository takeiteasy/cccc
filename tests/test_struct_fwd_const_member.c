// Regression: a "const struct S *" member naming a tag S that is completed
// later must resolve members through the completed definition.  Applying the
// const qualifier copies the (then incomplete) struct type; the canonical tag
// is completed in place afterwards, so member lookup now follows the type's
// origin to reach it.  This is the libsqlite "sqlite3_file::pMethods" idiom.

struct file {
    const struct io_methods *pMethods;  // io_methods is incomplete here
};

struct io_methods {                     // completed after first use above
    int (*xClose)(struct file *);
    int (*xRead)(struct file *, int);
};

static int do_close(struct file *f) { (void)f; return 40; }
static int do_read(struct file *f, int n) { (void)f; return n; }

static const struct io_methods METHODS = { do_close, do_read };

int main(void) {
    struct file f;
    f.pMethods = &METHODS;
    // Member access + call through the const-qualified, forward-referenced
    // pointer must find xClose / xRead.
    int r = f.pMethods->xClose(&f);     // 40
    r += f.pMethods->xRead(&f, 2);      // +2
    return r;                            // 42
}
