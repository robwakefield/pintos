#define FP_F 14

#define FP(n) ((n) << FP_F)

#define FP_FLOOR(x) ((x) >> FP_F)

#define FP_ROUND(x) (x >= 0 ? (x + (1 << (FP_F - 1))) >> FP_F : (x - (1 << (FP_F - 1))) >> FP_F) 

#define FP_ADD(x, y) (x + y)

#define FP_SUB(x, y) (x - y)

#define FP_MUL(x, y) ((((int64_t) x) * y) >> FP_F)

#define FP_DIV(x, y) ((((int64_t) x) << FP_F) / y)
