#!/usr/bin/env python3
"""Synthetic multi-TU corpus generator for the serializer type-dedup path (#1283).

`same_type_or_origin()` (src/serialize_type.c) is the multi-TU type-identity
predicate every type-registry dedup lookup in that file drives with a linear
scan. At whole-program (self-hosting) scale it became a performance wall
(#1283, child of #1132). This script emits a tunable N-TU x M-type C program
that exercises every arm of that predicate so its cost can be measured and the
fix proven to scale -- in seconds, instead of the ~1h self-hosting spike.

Every TU includes the same shared header (so the same tags/typedefs are parsed
once per TU and must dedup across all N), and additionally re-declares a batch
of file-scope `typedef enum`/anonymous `struct` shapes (the #1006/#1046 case:
origin-unrelated Types that must still compare structurally equal).

Usage:
    tools/gen_type_scale_corpus.py OUTDIR --tus N --types M
    cccc -c=native $OUTDIR/tu_*.c -I $OUTDIR -o /dev/null   # or -m, or -c=generated

Prints the list of generated .c files (one per line) to stdout.
"""
import argparse
import os
import sys
import textwrap

SHARED_HEADER = r"""
#pragma once
#include <stddef.h>

/* self-referential struct: drives same_type_or_origin's TY_PTR cycle-breaker */
struct scale_node {
    int key;
    struct scale_node *next;
    struct scale_node *prev;
};

/* function-pointer member: drives the TY_FUNC arm (#1233) */
typedef struct scale_ops {
    long (*apply)(struct scale_node *self, long acc);
    int  (*cmp)(const struct scale_node *a, const struct scale_node *b);
    void (*free)(struct scale_node *self);
} scale_ops;

/* deeply nested aggregate + array-of-aggregate: TY_ARRAY arm (#1046) */
typedef struct scale_matrix {
    struct { double v[4]; } rows[4];
    struct scale_node *owner;
} scale_matrix;

/* anonymous union member */
typedef struct scale_variant {
    int tag;
    union {
        long   as_int;
        double as_flt;
        void  *as_ptr;
        struct scale_node *as_node;
    };
} scale_variant;

enum scale_color { SCALE_RED, SCALE_GREEN = 7, SCALE_BLUE };

typedef struct scale_bundle {
    struct scale_node   node;
    scale_ops           ops;
    scale_matrix        mat;
    scale_variant       var;
    enum scale_color    col;
    struct scale_bundle *chain;
} scale_bundle;
"""

# Re-declared per TU: origin-unrelated Types with identical structure that
# same_type_or_origin must still dedup (the #1006 typedef enum / #1046 anon
# struct case). Parameterised by index so we can scale the count.
def repeated_shapes(m):
    out = []
    for i in range(m):
        out.append(textwrap.dedent(f"""\
            typedef enum {{ SHP{i}_A, SHP{i}_B = {i}, SHP{i}_C }} shape_enum_{i};
            typedef struct {{
                int          field_a;
                char         name[32];
                double       coords[3];
                shape_enum_{i} kind;
                struct scale_node *link;
            }} shape_rec_{i};
            struct tagged_shape_{i} {{
                shape_rec_{i} rec;
                shape_enum_{i} e;
            }};
        """))
    return "\n".join(out)


def tu_body(tu_idx, m):
    # Reference every shared + repeated shape by value so collect_type() walks
    # it and the dedup scans actually fire for each.
    uses = []
    for i in range(m):
        uses.append(f"    shape_rec_{i} r{i} = {{0}}; sink += r{i}.field_a + (int)r{i}.kind;")
        uses.append(f"    struct tagged_shape_{i} t{i} = {{0}}; sink += t{i}.e;")
    uses_block = "\n".join(uses)
    return textwrap.dedent(f"""\
        #include "scale_shared.h"
        #include "scale_repeated.h"

        static long consume_bundle(scale_bundle *b) {{
            long sink = 0;
            for (struct scale_node *n = &b->node; n; n = n->next)
                sink += n->key;
            sink += b->col;
            sink += (long)b->var.as_int;
            return sink;
        }}

        long scale_tu_{tu_idx}(scale_bundle *b) {{
            long sink = consume_bundle(b);
        {uses_block}
            return sink;
        }}
    """)


MAIN_BODY = textwrap.dedent("""\
    #include "scale_shared.h"
    #include "scale_repeated.h"

    {externs}

    int main(void) {{
        scale_bundle b = {{0}};
        long acc = 0;
    {calls}
        return (acc == acc) ? 42 : 1;
    }}
""")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--tus", type=int, default=16, help="number of translation units")
    ap.add_argument("--types", type=int, default=24,
                    help="repeated shape groups per TU (each = 1 enum + 2 aggregates)")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    with open(os.path.join(args.outdir, "scale_shared.h"), "w") as fh:
        fh.write(SHARED_HEADER)
    with open(os.path.join(args.outdir, "scale_repeated.h"), "w") as fh:
        fh.write("#pragma once\n#include \"scale_shared.h\"\n\n")
        fh.write(repeated_shapes(args.types))

    files = []
    for k in range(args.tus):
        p = os.path.join(args.outdir, f"tu_{k}.c")
        with open(p, "w") as fh:
            fh.write(tu_body(k, args.types))
        files.append(p)

    externs = "\n".join(f"long scale_tu_{k}(scale_bundle *);" for k in range(args.tus))
    calls = "\n".join(f"    acc += scale_tu_{k}(&b);" for k in range(args.tus))
    p = os.path.join(args.outdir, "main.c")
    with open(p, "w") as fh:
        fh.write(MAIN_BODY.format(externs=externs, calls=calls))
    files.append(p)

    for f in files:
        print(f)


if __name__ == "__main__":
    main()
