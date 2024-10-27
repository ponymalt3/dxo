#pragma once

#include <stdint.h>

#include <algorithm>
#include <cassert>
#include <complex>
#include <iostream>
#include <list>
#include <span>
#include <vector>

#include "../tasks/tasks.h"
#include "fft.h"
using ComplexVec = std::vector<std::complex<float>>;
using RealVec = std::vector<float>;
using TaskType = std::shared_ptr<Task>;

static void multiply(std::complex<float>* result,
                     const std::complex<float>* src1,
                     const std::complex<float>* src2,
                     uint32_t size)
{
  auto s1 = src1;
  auto s2 = src2;
  auto r = result;
  auto rend = result + size;

  while(r < rend)
  {
    *r = (*s1) * (*s2);
    ++s1;
    ++s2;
    ++r;
  }
}

static void multiplyAdd(std::complex<float>* result,
                        const std::complex<float>* src1,
                        const std::complex<float>* src2,
                        uint32_t size)
{
  auto s1 = src1;
  auto s2 = src2;
  auto r = result;
  auto rend = result + size;

  while(r < rend)
  {
    *r += (*s1) * (*s2);
    ++s1;
    ++s2;
    ++r;
  }
}

static void add(std::complex<float>* result,
                const std::complex<float>* src1,
                const std::complex<float>* src2,
                uint32_t size)
{
  auto s1 = src1;
  auto s2 = src2;
  auto r = result;
  auto rend = result + size;

  while(r < rend)
  {
    *r = (*s1) + (*s2);
    ++s1;
    ++s2;
    ++r;
  }
}

class Convolution
{
public:
  Convolution(const RealData& h, uint32_t subFilterSize) : Convolution(h, subFilterSize, subFilterSize) {}
  Convolution(const RealData& h, uint32_t subFilterSize, uint32_t inputBlockSize)
      : subFilterSize_{subFilterSize},
        inputBlockSize_{inputBlockSize},
        fftSize_{inputBlockSize_ + subFilterSize_ - 1},
        forwardFft_{fftSize_},
        inverseFft_{fftSize_}
  {
    blockSize_ = forwardFft_.output_.size();
    numBlocks_ = h.size() / subFilterSize;
    assert(numBlocks > 1 && "must be more than one block");
    assert(numBlocks_ * subFilterSize == h.size() && "length(h) must be multiple of subFilterSize ");
    H_ = new(std::align_val_t(64)) std::complex<float>[blockSize_ * numBlocks_];
    delayLine_ = new(std::align_val_t(64)) std::complex<float>[blockSize_ * numBlocks_];
    XX_ = new(std::align_val_t(64)) float[subFilterSize_];
    memset(XX_, 0, sizeof(float) * subFilterSize_);

    // clear delay line
    for(uint32_t i{0}; i < numBlocks_; ++i)
    {
      memset(getDataBlock(i), 0, blockSize_ * sizeof(std::complex<float>));
    }

    ForwardFFT fft{fftSize_};
    float* src = h.data();
    std::complex<float>* dst = H_;
    for(uint32_t i{0}; i < numBlocks_; ++i)
    {
      for(auto& f : fft.input_.subspan(0, subFilterSize_))
      {
        f = *(src++);
      }

      for(auto& f : fft.input_.subspan(subFilterSize_))
      {
        f = 0.0f;
      }

      fft.run();

      std::cout << "H[" << (i) << "]:\n";
      for(auto f : fft.output_)
      {
        *(dst++) = f;
        std::cout << f << " ";
      }
      std::cout << std::endl;
    }
  }

  ~Convolution()
  {
    delete[] H_;
    delete[] delayLine_;
    delete[] XX_;
  }

  std::tuple<TaskType, RealData> getInputTask()
  {
    auto fft = Task::create<ComplexData>(
        [this](Task& task) {
          memcpy(forwardFft_.input_.data(), XX_, (subFilterSize_ - 1) * sizeof(float));
          auto x = forwardFft_.input_.last(subFilterSize_ - 1);
          memcpy(XX_, x.data(), x.size() * sizeof(float));

          std::cout << "Input size: " << (forwardFft_.input_.size()) << std::endl;
          for(auto& i : forwardFft_.input_)
          {
            std::cout << (i) << " ";
          }
          std::cout << std::endl;
          forwardFft_.run();

          std::cout << "resul: " << std::endl;
          for(auto& i : task.getArtifact<ComplexData>())
          {
            std::cout << (i) << " ";
          }
          std::cout << std::endl;
        },
        {},
        forwardFft_.output_.subspan(0));

    return {fft, forwardFft_.input_.subspan(subFilterSize_ - 1)};
  }

  std::tuple<std::vector<TaskType>, RealData> getOutputTasks(TaskType input, uint32_t combineBlocks = 4)
  {
    std::cout << "numBlocks_: " << (numBlocks_) << std::endl;

    auto rootTask = Task::create<uint32_t>([](Task& task) {});
    // first level multiply and add
    std::vector<TaskType> deps;
    deps.push_back(input);
    for(uint32_t i{1}; i < numBlocks_; i += combineBlocks)
    {
      deps.push_back(Task::create<ComplexVec>(
          [this, i, combineBlocks](Task& task) {
            multiplyAddBlocks(i, task.getArtifact<ComplexVec>(), combineBlocks);
            std::cout << "sum: \n";
            for(auto& x : task.getArtifact<ComplexVec>())
            {
              std::cout << (x) << " ";
            }
            std::cout << std::endl;
          },
          {rootTask},
          ComplexVec(blockSize_)));
    }

    std::cout << "complete 1" << std::endl;

    // move block
    auto shift = Task::create<ComplexData>(
        [this](Task& task) { pushDataBlock(task.getDependencies()[0]->getArtifact<ComplexData>().data()); },
        deps);

    std::cout << "deps: " << (deps.size()) << std::endl;

    std::list<TaskType> sumUpTasks{deps.begin() + 1, deps.end()};
    std::cout << "sumUpTasks: " << (sumUpTasks.size()) << std::endl;

    while(sumUpTasks.size() > 1)
    {
      uint32_t numDeps = std::min<uint32_t>(std::max<uint32_t>(2, combineBlocks), sumUpTasks.size());
      std::cout << "  numDeps: " << (numDeps) << std::endl;

      std::vector<TaskType> deps;
      for(uint32_t i{0}; i < numDeps; ++i)
      {
        deps.push_back(sumUpTasks.front());
        sumUpTasks.pop_front();
        std::cout << "  pop " << std::endl;
      }

      sumUpTasks.push_back(Task::create<ComplexVec>(
          [this](Task& task) {
            sumBlocks(task.getArtifact<ComplexVec>(), task.getDependencies());
            std::cout << "sumUP: \n";
            for(auto& x : task.getArtifact<ComplexVec>())
            {
              std::cout << (x) << " ";
            }
            std::cout << std::endl;
          },
          deps,
          ComplexVec(blockSize_)));
      std::cout << "  push " << std::endl;
    }

    std::cout << "complete 3" << std::endl;

    auto combine = Task::create<ComplexData>(
        [this](Task& task) {
          std::cout << "combine:" << std::endl;
          auto result = task.getArtifact<ComplexData>().data();
          multiply(result, H_, task.getDependencies()[0]->getArtifact<ComplexVec>().data(), blockSize_);
          // add(result, result, task.getDependencies()[1]->getArtifact<ComplexVec>().data(), blockSize_);
          for(auto& x : task.getArtifact<ComplexData>())
          {
            std::cout << (x) << " ";
          }
          std::cout << std::endl;
          // std::cout << "dependents_\n";
          for(auto& x : task.dependents_)
          {
            // std::cout << "dependent: " << x->dependenciesLeft_ << std::endl;
          }
        },
        {input},  //, sumUpTasks.front()},
        inverseFft_.input_.subspan(0));

    auto resultTask = Task::create<RealData>(
        [this](Task& task) {
          std::cout << "inverse input:\n";
          for(auto& x : inverseFft_.input_)
          {
            std::cout << (x) << " ";
          }
          std::cout << std::endl;
          inverseFft_.run();
          std::cout << "inverse output:\n";
          for(auto& x : inverseFft_.output_)
          {
            std::cout << (x) << " ";
          }
          std::cout << std::endl;
        },
        {combine, shift},
        inverseFft_.output_.subspan(subFilterSize_ - 1));

    return {{rootTask, resultTask}, resultTask->getArtifact<RealData>()};
  }

protected:
  void multiplyAddBlocks(uint32_t index, ComplexVec& result, uint32_t numBlocks = 1) const
  {
    multiply(result.data(), H_ + (blockSize_ * index), getDataBlock(index), blockSize_);

    auto maxIndex = std::min<uint32_t>(numBlocks_, index + numBlocks);
    while(++index < maxIndex)
    {
      multiplyAdd(result.data(), H_ + (blockSize_ * index), getDataBlock(index), blockSize_);
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

  void pushDataBlock(std::complex<float>* Hdata)
  {
    std::cout << "shift\n";
    for(uint32_t i{0}; i < numBlocks_; ++i)
    {
      std::cout << "  block[" << (i) << "]:  ";
      for(uint32_t j{0}; j < blockSize_; ++j)
      {
        std::cout << (getDataBlock(i)[j]) << " ";
      }
      std::cout << std::endl;
    }
    std::cout << std::endl;

    memcpy(getDataBlock(0), Hdata, blockSize_ * sizeof(Hdata[0]));
    firstBlock_ = (++firstBlock_ == numBlocks_) ? 0 : firstBlock_;
  }

  uint32_t getNumBlocks() const { return numBlocks_; }

  std::complex<float>* getDataBlock(uint32_t index)
  {
    auto i = static_cast<int32_t>(index) - firstBlock_;
    i += (i < 0) ? numBlocks_ : 0;
    return delayLine_ + i * blockSize_;
  }

  uint32_t inputBlockSize_;
  uint32_t subFilterSize_;
  uint32_t fftSize_;
  int32_t numBlocks_;
  uint32_t blockSize_;
  float* XX_;
  std::complex<float>* H_;
  std::complex<float>* delayLine_;
  int32_t firstBlock_{0};
  ForwardFFT forwardFft_;
  BackwardFFT inverseFft_;
};