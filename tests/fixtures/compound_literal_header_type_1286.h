#ifndef FIXTURE_COMPOUND_LITERAL_HEADER_TYPE_1286_H
#define FIXTURE_COMPOUND_LITERAL_HEADER_TYPE_1286_H

// #1286 regression fixture: both a tagged and an untagged aggregate,
// declared in an *included* header rather than the primary file -- the
// bug only fires when the file-scope pointer initializer's compound
// literal names a type whose declaration lives outside the command-line
// input.
typedef struct TaggedPoint1286 {
    int x;
    int y;
} TaggedPoint1286;

typedef struct {
    int kind;
    int size;
} UntaggedThing1286;

#endif
