#include <stdio.h>
#include <stdlib.h>

#ifndef BENCH_W
#define BENCH_W 400
#endif
#ifndef BENCH_H
#define BENCH_H 400
#endif
#ifndef BENCH_MAX_ITER
#define BENCH_MAX_ITER 200
#endif

int main(void) {
    long w        = BENCH_W;
    long h        = BENCH_H;
    long max_iter = BENCH_MAX_ITER;
    long total    = 0;
    for (long py = 0; py < h; py++) {
        double y0 = (double)py / (double)h * 2.5 - 1.25;
        for (long px = 0; px < w; px++) {
            double x0   = (double)px / (double)w * 2.0 - 2.0;
            double x    = 0.0;
            double y    = 0.0;
            long   iter = 0;
            while (iter < max_iter && x * x + y * y <= 4.0) {
                double xt = x * x - y * y + x0;
                y         = 2.0 * x * y + y0;
                x         = xt;
                iter++;
            }
            total += iter;
        }
    }
    printf("result: %ld\n", total);
    return 42;
}
