#include <stdint.h>

#include <complex>
#include <iostream>

#include "../crossover/neon.h"

static void multiply(std::complex<float>* result,
                     const std::complex<float>* src1,
                     const std::complex<float>* src2)
{
  auto s1 = src1;
  auto s2 = src2;
  auto r = result;
  auto rend = result + 4;

  while(r < rend)
  {
    *r++ = *s1++ * *s2++;
  }
}

static void multiplyAdd(std::complex<float>* result,
                        const std::complex<float>* src1,
                        const std::complex<float>* src2)
{
  auto s1 = src1;
  auto s2 = src2;
  auto r = result;
  auto rend = result + 4;

  while(r < rend)
  {
    *r++ += *s1++ * *s2++;
  }
}

static void add(std::complex<float>* result, const std::complex<float>* src1, const std::complex<float>* src2)
{
  auto s1 = src1;
  auto s2 = src2;
  auto r = result;
  auto rend = result + 4;

  while(r < rend)
  {
    *r++ = *s1++ + *s2++;
  }
}

int main()
{
  /*float re[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  float im[8] = {10, 11, 12, 13, 14, 15, 16, 17};
  float result_re[4] = {0, 0, 0, 0};
  float result_im[4] = {0, 0, 0, 0};

  complexMul(re, im, result_re, result_im);
  for(int i = 0; i < 4; ++i)
  {
    std::complex<float> a{re[i], im[i]}, b{re[i + 4], im[i + 4]}, r = a * b;
    std::cout << "complex " << r << "  {" << result_re[i] << ", " << result_im[i] << "}" << std::endl;
  }*/

  std::complex<float> a[4] = {{1, 10}, {2, 11}, {3, 12}, {4, 13}};
  std::complex<float> b[4] = {{5, 14}, {6, 15}, {7, 16}, {8, 17}};
  std::complex<float> result[4] = {{99, 11}, {45, 33}, {32, 72}, {48, 56}};
  std::complex<float> result2[4] = {{99, 11}, {45, 33}, {32, 72}, {48, 56}};

  // complexMul2(result, a, b);
  // multiply(result2, a, b);
  neon::add(result, a, b);
  add(result2, a, b);

  std::cout << "complexMul2: ";
  for(auto i : result)
  {
    std::cout << i << ", ";
  }
  std::cout << "\nmul2: ";
  for(auto i : result2)
  {
    std::cout << i << ", ";
  }
  std::cout << std::endl;

  return 0;
}
