#include <stdint.h>

#include <complex>
#include <iostream>
#include <vector>

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
  std::complex<float> a[4] = {{1, 10}, {2, 11}, {3, 12}, {4, 13}};
  std::complex<float> b[4] = {{5, 14}, {6, 15}, {7, 16}, {8, 17}};
  std::vector<std::complex<float>> result{{0, 0}, {0, 0}, {0, 0}, {0, 0}};
  std::vector<std::complex<float>> result_neon{{0, 0}, {0, 0}, {0, 0}, {0, 0}};

  neon::add(result_neon.data(), a, b);
  add(result.data(), a, b);
  const bool addEqual = result_neon == result;

  neon::multiply(result_neon.data(), a, b);
  multiply(result.data(), a, b);
  const bool multiplyEqual = result_neon == result;

  neon::multiplyAdd(result_neon.data(), a, b);
  multiplyAdd(result.data(), a, b);
  const bool multiplyAddEqual = result_neon == result;

  return (addEqual && multiplyEqual && multiplyAddEqual) ? 0 : -1;
}
