#define FP(n) (n * 14)

#define FP_FLOOR(x) (x / 14)

#define FP_ROUND(x) (x >= 0 ? (x + (14 / 2)) / 14 : (x - (14 / 2)) / 14) 

#define FP_ADD(x, y) (x + y)

#define FP_SUB(x, y) (x - y)

#define FP_MUL(x, y) ((((int64_t) x) * y) / 14)

#define FP_DIV(x, y) ((((int64_t) x) * 14) / y)
