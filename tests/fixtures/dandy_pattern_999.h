typedef unsigned long DPValue;
static inline DPValue dp_box(unsigned long x) {
    return x;
}
typedef struct {
    int (*open)(void);
    int (*close)(void);
} DPVT;
