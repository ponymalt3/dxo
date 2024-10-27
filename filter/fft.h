#pragma once

#include <fftw3.h>
#include <stdint.h>

#include <complex>
#include <iostream>
#include <span>
#include <vector>

using ComplexData = std::span<std::complex<float>>;
using RealData = std::span<float>;

struct ForwardFFT
{
public:
  ForwardFFT(uint32_t size, bool measure = true)
      : input_{new(std::align_val_t(64)) float[size], size},
        output_{new(std::align_val_t(64)) std::complex<float>[size / 2 + 1], size / 2 + 1}
  {
    plan_ = fftwf_plan_dft_r2c_1d(size,
                                  input_.data(),
                                  reinterpret_cast<fftwf_complex*>(output_.data()),
                                  measure ? FFTW_MEASURE : FFTW_ESTIMATE);
  }

  ~ForwardFFT()
  {
    fftwf_destroy_plan(plan_);
    delete[] input_.data();
    delete[] output_.data();
  }

  void run() { fftwf_execute(plan_); }

  RealData input_;
  ComplexData output_;
  fftwf_plan plan_;
};

struct BackwardFFT
{
public:
  BackwardFFT(uint32_t size)
      : input_{new(std::align_val_t(64)) std::complex<float>[size / 2 + 1], size / 2 + 1},
        output_{new(std::align_val_t(64)) float[size], size}
  {
    plan_ = fftwf_plan_dft_c2r_1d(
        size, reinterpret_cast<fftwf_complex*>(input_.data()), output_.data(), FFTW_MEASURE);
  }
  ~BackwardFFT()
  {
    fftwf_destroy_plan(plan_);
    delete[] input_.data();
    delete[] output_.data();
  }

  void run() { fftwf_execute(plan_); }

  ComplexData input_;
  RealData output_;
  fftwf_plan plan_;
};