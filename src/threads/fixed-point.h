int fp_multiplication (int a, int b) {
  return (((int64_t) a) * b ) >> 14;
}

int fp_division (int a, int b) {
  return (((int64_t) a) << 14) / b;
}

int fp (int a) {
  return a << 14;
}

int fp_int (int a) {
  if (a >= 0) {
    a = (a + (1 << 13));
  } else {
    a = (a - (1 << 13));
  }
  return ((signed int) a) >> 14;
}
