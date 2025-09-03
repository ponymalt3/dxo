#pragma once

#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <list>
#include <span>
#include <vector>

#include "../tasks/tasks.h"
#include "fft.h"

#ifdef BUILD_ARM
#include "neon.h"
#endif

using ComplexVec = std::vector<std::complex<float>>;
using RealVec = std::vector<float>;
using TaskType = std::shared_ptr<Task>;

static_assert(sizeof(std::complex<float>) == sizeof(fftwf_complex));

inline void multiply(std::complex<float>* result,
                     const std::complex<float>* src1,
                     const std::complex<float>* src2,
                     uint32_t size)
{
  auto s1 = src1;
  auto s2 = src2;
  auto r = result;
  auto rend = result + size;

  if((size & 1) && ((size - 1) % 8) == 0)
  {
    --rend;
    while(r < rend)
    {
#ifdef BUILD_ARM
      neon::multiply(r, s1, s2);
      neon::multiply(r + 4, s1 + 4, s2 + 4);
#else
      r[0] = s1[0] * s2[0];
      r[1] = s1[1] * s2[1];
      r[2] = s1[2] * s2[2];
      r[3] = s1[3] * s2[3];
      r[4] = s1[4] * s2[4];
      r[5] = s1[5] * s2[5];
      r[6] = s1[6] * s2[6];
      r[7] = s1[7] * s2[7];
#endif
      s1 += 8;
      s2 += 8;
      r += 8;
    }

    r[0] = s1[0] * s2[0];
  }
  else
  {
    while(r < rend)
    {
      *r++ = *s1++ * *s2++;
    }
  }
}

inline void multiplyAdd(std::complex<float>* result,
                        const std::complex<float>* src1,
                        const std::complex<float>* src2,
                        uint32_t size)
{
  auto s1 = src1;
  auto s2 = src2;
  auto r = result;
  auto rend = result + size;

  if((size & 1) && ((size - 1) % 8) == 0)
  {
    --rend;
    while(r < rend)
    {
#ifdef BUILD_ARM
      neon::multiplyAdd(r, s1, s2);
      neon::multiplyAdd(r + 4, s1 + 4, s2 + 4);
#else
      r[0] += s1[0] * s2[0];
      r[1] += s1[1] * s2[1];
      r[2] += s1[2] * s2[2];
      r[3] += s1[3] * s2[3];
      r[4] += s1[4] * s2[4];
      r[5] += s1[5] * s2[5];
      r[6] += s1[6] * s2[6];
      r[7] += s1[7] * s2[7];
#endif
      s1 += 8;
      s2 += 8;
      r += 8;
    }

    r[0] += s1[0] * s2[0];
  }
  else
  {
    while(r < rend)
    {
      *r++ += *s1++ * *s2++;
    }
  }
}

inline void add(std::complex<float>* result,
                const std::complex<float>* src1,
                const std::complex<float>* src2,
                uint32_t size)
{
  auto s1 = src1;
  auto s2 = src2;
  auto r = result;
  auto rend = result + size;

  if((size & 1) && ((size - 1) % 8) == 0)
  {
    --rend;
    while(r < rend)
    {
#ifdef BUILD_ARM
      neon::add(r, s1, s2);
      neon::add(r + 4, s1 + 4, s2 + 4);
#else
      r[0] = s1[0] + s2[0];
      r[1] = s1[1] + s2[1];
      r[2] = s1[2] + s2[2];
      r[3] = s1[3] + s2[3];
      r[4] = s1[4] + s2[4];
      r[5] = s1[5] + s2[5];
      r[6] = s1[6] + s2[6];
      r[7] = s1[7] + s2[7];
#endif
      s1 += 8;
      s2 += 8;
      r += 8;
    }

    r[0] = s1[0] + s2[0];
  }
  else
  {
    while(r < rend)
    {
      *r++ = *s1++ + *s2++;
    }
  }
}

class Convolution
{
public:
  Convolution(const std::span<const float>& h, uint32_t inputBlockSize)
      : subFilterSize_{inputBlockSize}, fftSize_{inputBlockSize + subFilterSize_}, inverseFft_{fftSize_}
  {
    blockSize_ = fftSize_ / 2 + 1;
    numBlocks_ = (h.size() + subFilterSize_ - 1) / subFilterSize_;

    H_ = new(std::align_val_t(64))
        std::complex<float>[blockSize_ * numBlocks_];  // make each block cache line aligned
    delayLine_ = new(std::align_val_t(64)) std::complex<float>[blockSize_ * numBlocks_];

    clearDelayLine();
    transformFilterCoeffs(h);
  }

  ~Convolution()
  {
    delete[] H_;
    delete[] delayLine_;
  }

  static std::tuple<TaskType, RealData> getInputTask(uint32_t inputBlockSize)
  {
    auto subFilterSize = inputBlockSize;
    auto forwardFft = std::make_shared<ForwardFFT>(inputBlockSize + subFilterSize);
    auto overlapBuffer = std::shared_ptr<float>(new(std::align_val_t(64)) float[subFilterSize]);  // align mem
    memset(overlapBuffer.get(), 0, sizeof(float) * subFilterSize);

    auto fft = Task::create<ComplexData>(
        [subFilterSize, inputBlockSize, forwardFft, overlapBuffer](Task& task) {
          auto inputBuffer = forwardFft->input_.last(subFilterSize);
          memcpy(forwardFft->input_.data(), overlapBuffer.get(), (subFilterSize) * sizeof(float));
          memcpy(overlapBuffer.get(), inputBuffer.data(), inputBuffer.size() * sizeof(float));
          forwardFft->run();
        },
        {},
        forwardFft->output_.subspan(0));

    return {fft, forwardFft->input_.last(inputBlockSize)};
  }

  std::tuple<std::vector<TaskType>, RealData> getOutputTasks(TaskType input, uint32_t combineBlocks = 4)
  {
    auto rootTask = Task::create<uint32_t>([](Task& task) {});

    // first level multiply and add
    std::vector<TaskType> deps;
    deps.push_back(input);
    for(uint32_t i{1}; i < numBlocks_; i += combineBlocks)
    {
      deps.push_back(Task::create<ComplexVec>(
          [this, i, combineBlocks](Task& task) {
            multiplyAddBlocks(i, task.getArtifact<ComplexVec>(), combineBlocks);
          },
          {rootTask},
          ComplexVec(blockSize_)));
    }

    // move block
    auto shift = Task::create<ComplexData>(
        [this](Task& task) { pushBlock(task.getDependencies()[0]->getArtifact<ComplexData>().data()); },
        deps);

    std::list<TaskType> sumUpTasks{deps.begin() + 1, deps.end()};
    while(sumUpTasks.size() > 1)
    {
      uint32_t numDeps = std::min<uint32_t>(std::max<uint32_t>(2, combineBlocks), sumUpTasks.size());

      std::vector<TaskType> deps;
      for(uint32_t i{0}; i < numDeps; ++i)
      {
        deps.push_back(sumUpTasks.front());
        sumUpTasks.pop_front();
      }

      sumUpTasks.push_back(Task::create<ComplexVec>(
          [this](Task& task) { sumBlocks(task.getArtifact<ComplexVec>(), task.getDependencies()); },
          deps,
          ComplexVec(blockSize_)));
    }

    TaskType combine = nullptr;
    if(sumUpTasks.size() > 0)
    {
      combine = Task::create<ComplexData>(
          [this](Task& task) {
            auto result = task.getArtifact<ComplexData>().data();
            for(auto& _ : std::span(H_, blockSize_))
            {
              multiply(result, H_, task.getDependencies()[0]->getArtifact<ComplexData>().data(), blockSize_);
              add(result, result, task.getDependencies()[1]->getArtifact<ComplexVec>().data(), blockSize_);
            }
          },
          {input, sumUpTasks.front()},
          inverseFft_.input_.subspan(0));
    }
    else
    {
      // just one block => no need to sum up blocks
      combine = Task::create<ComplexData>(
          [this](Task& task) {
            auto result = task.getArtifact<ComplexData>().data();
            for(auto& _ : std::span(H_, blockSize_))
            {
              multiply(result, H_, task.getDependencies()[0]->getArtifact<ComplexData>().data(), blockSize_);
            }
          },
          {input},
          inverseFft_.input_.subspan(0));
    }

    auto resultTask = Task::create<RealData>([this](Task& task) { inverseFft_.run(); },
                                             {combine, shift},
                                             inverseFft_.output_.subspan(subFilterSize_));

    return {{rootTask, resultTask}, resultTask->getArtifact<RealData>()};
  }

  void clearDelayLine() { memset(delayLine_, 0, blockSize_ * numBlocks_ * sizeof(delayLine_[0])); }

protected:
  static uint32_t getSubFilterSize(uint32_t inputBlockSize)
  {
    return (1 << static_cast<uint32_t>(std::ceil(std::log2(inputBlockSize) + 1))) - inputBlockSize;
  }

  void transformFilterCoeffs(const std::span<const float> h)
  {
    ForwardFFT fft{fftSize_};

    const float* src = h.data();
    std::complex<float>* dst = H_;
    for(uint32_t i{0}; i < numBlocks_; ++i)
    {
      for(auto& f : fft.input_.subspan(0, subFilterSize_))
      {
        f = src < (h.data() + h.size()) ? *src++ / fftSize_ : 0.0f;
      }

      for(auto& f : fft.input_.subspan(subFilterSize_))
      {
        f = 0.0f;
      }

      fft.run();

      for(auto f : fft.output_)
      {
        *(dst++) = f;
      }
    }
  }

  void multiplyAddBlocks(uint32_t index, ComplexVec& result, uint32_t numBlocks = 1) const
  {
    multiply(result.data(), H_ + (blockSize_ * index), getBlock(index), blockSize_);

    auto maxIndex = std::min<uint32_t>(numBlocks_, index + numBlocks);
    while(++index < maxIndex)
    {
      multiplyAdd(result.data(), H_ + (blockSize_ * index), getBlock(index), blockSize_);
    }
  }

  void sumBlocks(ComplexVec& result, const std::vector<TaskType>& operands) const
  {
    add(result.data(),
        operands[0]->getArtifact<ComplexData>().data(),
        operands[1]->getArtifact<ComplexData>().data(),
        blockSize_);

    for(uint32_t i{2}; i < operands.size(); ++i)
    {
      add(result.data(), result.data(), operands[i]->getArtifact<ComplexData>().data(), blockSize_);
    }
  }

  void pushBlock(std::complex<float>* Hdata)
  {
    memcpy(getBlock(0), Hdata, blockSize_ * sizeof(Hdata[0]));
    firstBlock_ = (++firstBlock_ == numBlocks_) ? 0 : firstBlock_;
  }

  uint32_t getNumBlocks() const { return numBlocks_; }

  std::complex<float>* getBlock(uint32_t index) const
  {
    auto i = static_cast<int32_t>(index) - firstBlock_;
    i += (i < 0) ? numBlocks_ : 0;
    return delayLine_ + i * blockSize_;
  }

  uint32_t subFilterSize_;
  uint32_t fftSize_;
  int32_t numBlocks_;
  uint32_t blockSize_;
  std::complex<float>* H_;
  std::complex<float>* delayLine_;
  int32_t firstBlock_{0};
  BackwardFFT inverseFft_;
};