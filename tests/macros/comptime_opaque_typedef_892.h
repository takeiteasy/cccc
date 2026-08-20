#ifndef COMPTIME_OPAQUE_TYPEDEF_892_H
#define COMPTIME_OPAQUE_TYPEDEF_892_H
// Fixture for #892: the near-universal opaque-handle C idiom -- forward-
// declare a struct and typedef it to its own name -- must keep working once
// @shared into a [[cccc::comptime]]-using file. same_type_or_origin()
// (serialize.c) used to treat every opaque/incomplete struct as "the same
// type" as every other, so this collapsed onto whichever opaque tag
// happened to appear first in scope (one of reflection.h's own forward
// declarations, e.g. AttrTarget). This header is @shared'd from the test
// below, exercising the reported failure directly.

typedef struct Widget Widget;
typedef struct Gadget Gadget;

struct Widget {
    int tag;
};
struct Gadget {
    int tag;
};

static inline Widget *make_widget(void) {
    static struct Widget w = {1};
    return &w;
}

static inline Gadget *make_gadget(void) {
    static struct Gadget g = {2};
    return &g;
}

static inline int widget_tag(Widget *w) {
    return w->tag;
}
static inline int gadget_tag(Gadget *g) {
    return g->tag;
}

#endif
