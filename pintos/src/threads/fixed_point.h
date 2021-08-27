#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H
#include <stdint.h>

typedef int32_t fp32_t;

#define FP32_SIZE 32
#define FP32_P 17
#define FP32_Q (FP32_SIZE - FP32_P - 1)
#define FP32_F (1 << (FP32_Q))

static fp32_t
int_to_fp32 (int32_t n)
{
  return n * FP32_F;
}

static int32_t
fp32_to_int_0 (fp32_t x)
{
  return x / FP32_F;
}

static fp32_t
fp32_to_int (fp32_t x)
{
  return x > 0 ? fp32_to_int_0 (x + FP32_F / 2)
               : fp32_to_int_0 (x - FP32_F / 2);
}

static fp32_t
fp32_mul (fp32_t x, fp32_t y)
{
  return (fp32_t)(((int64_t)x) * y / FP32_F);
}

static fp32_t
fp32_mul_int (fp32_t x, int32_t n)
{
  return x * n;
}

static fp32_t
fp32_div (fp32_t x, fp32_t y)
{
  return (fp32_t)(((int64_t)x) * FP32_F / y);
}

static fp32_t
fp32_div_int (fp32_t x, int32_t y)
{
  return x / y;
}
#endif