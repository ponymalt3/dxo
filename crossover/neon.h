#pragma once

#include <arm_neon.h>

namespace neon
{

inline void mul(std::complex<float>* __restrict result,
                const std::complex<float>* __restrict src1,
                const std::complex<float>* __restrict src2)
{
  auto a = vld2q_f32(reinterpret_cast<const float* __restrict>(src1));
  auto b = vld2q_f32(reinterpret_cast<const float* __restrict>(src2));

  auto re = vmlsq_f32(vmulq_f32(a.val[0], b.val[0]), a.val[1], b.val[1]);
  auto im = vmlaq_f32(vmulq_f32(a.val[0], b.val[1]), a.val[1], b.val[0]);

  vst2q_f32(reinterpret_cast<float* __restrict>(result), float32x4x2_t{re, im});
}

void multiplyAdd(std::complex<float>* __restrict result,
                 const std::complex<float>* __restrict src1,
                 const std::complex<float>* __restrict src2)
{
  auto a = vld2q_f32(reinterpret_cast<const float* __restrict>(src1));
  auto b = vld2q_f32(reinterpret_cast<const float* __restrict>(src2));
  auto sum = vld2q_f32(reinterpret_cast<const float* __restrict>(result));

  sum.val[0] = vmlaq_f32(sum.val[0], a.val[0], b.val[0]);
  sum.val[1] = vmlaq_f32(sum.val[1], a.val[1], b.val[0]);
  sum.val[0] = vmlsq_f32(sum.val[0], a.val[1], b.val[1]);
  sum.val[1] = vmlaq_f32(sum.val[1], a.val[0], b.val[1]);

  vst2q_f32(reinterpret_cast<float* __restrict>(result), sum);
}

void add(std::complex<float>* __restrict result,
         const std::complex<float>* __restrict src1,
         const std::complex<float>* __restrict src2)
{
  auto a = vld2q_f32(reinterpret_cast<const float* __restrict>(src1));
  auto b = vld2q_f32(reinterpret_cast<const float* __restrict>(src2));

  vst2q_f32(reinterpret_cast<float* __restrict>(result),
            float32x4x2_t{vaddq_f32(a.val[0], b.val[0]), vaddq_f32(a.val[1], b.val[1])});
}

}  // namespace neon