#pragma once

#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <list>
#include <span>
#include <vector>

#include "../tasks/tasks.h"
#include "fft.h"

using ComplexVec = std::vector<std::complex<float>>;
using RealVec = std::vector<float>;
using TaskType = std::shared_ptr<Task>;

static_assert(sizeof(std::complex<float>) == sizeof(fftwf_complex));

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
  Convolution(const std::span<const float>& h, uint32_t inputBlockSize)
      : subFilterSize_{inputBlockSize}, fftSize_{inputBlockSize + subFilterSize_}, inverseFft_{fftSize_}
  {
    blockSize_ = fftSize_ / 2 + 1;
    numBlocks_ = (h.size() + subFilterSize_ - 1) / subFilterSize_;
    assert(numBlocks_ > 1 && "must be more than one block");
    //  assert(subFilterSize_ >= inputBlockSize && "subFilterSize must be greater than inputBlockSize");
    H_ = new(std::align_val_t(64)) std::complex<float>[blockSize_ * numBlocks_];
    delayLine_ = new(std::align_val_t(64)) std::complex<float>[blockSize_ * numBlocks_];

    // clear delay line
    for(uint32_t i{0}; i < numBlocks_; ++i)
    {
      memset(getDataBlock(i), 0, blockSize_ * sizeof(std::complex<float>));
    }

    ForwardFFT fft{fftSize_};
    const float* src = h.data();
    std::complex<float>* dst = H_;
    for(uint32_t i{0}; i < numBlocks_; ++i)
    {
      for(auto& f : fft.input_.subspan(0, subFilterSize_))
      {
        if(src < h.data() + h.size())
        {
          f = *(src++) / fftSize_;
        }
        else  // pad length(h) to be multiple of subFilterSize
        {
          f = 0.0f;
        }
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
          auto x = forwardFft->input_.last(subFilterSize);
          memcpy(forwardFft->input_.data(), overlapBuffer.get(), (subFilterSize) * sizeof(float));
          x = forwardFft->input_.last(subFilterSize);
          memcpy(overlapBuffer.get(), x.data(), x.size() * sizeof(float));
          /*std::cout << "input: " << std::endl;
          for(auto& i : forwardFft->input_)
          {
            std::cout << (i) << " ";
          }
          std::cout << std::endl;*/
          forwardFft->run();

          /*std::cout << "resul: " << std::endl;
          for(auto& i : task.getArtifact<ComplexData>())
          {
            std::cout << (i) << " ";
          }
          std::cout << std::endl;*/
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
        [this](Task& task) { pushDataBlock(task.getDependencies()[0]->getArtifact<ComplexData>().data()); },
        deps);

    // std::cout << "deps: " << (deps.size()) << std::endl;

    std::list<TaskType> sumUpTasks{deps.begin() + 1, deps.end()};
    // std::cout << "sumUpTasks: " << (sumUpTasks.size()) << std::endl;

    while(sumUpTasks.size() > 1)
    {
      uint32_t numDeps = std::min<uint32_t>(std::max<uint32_t>(2, combineBlocks), sumUpTasks.size());
      // std::cout << "  numDeps: " << (numDeps) << std::endl;

      std::vector<TaskType> deps;
      for(uint32_t i{0}; i < numDeps; ++i)
      {
        deps.push_back(sumUpTasks.front());
        sumUpTasks.pop_front();
        // std::cout << "  pop " << std::endl;
      }

      sumUpTasks.push_back(Task::create<ComplexVec>(
          [this](Task& task) {
            sumBlocks(task.getArtifact<ComplexVec>(), task.getDependencies());
            /*std::cout << "sumUP: \n";
            for(auto& x : task.getArtifact<ComplexVec>())
            {
              std::cout << (x) << " ";
            }
            std::cout << std::endl;*/
          },
          deps,
          ComplexVec(blockSize_)));
      // std::cout << "  push " << std::endl;
    }

    // std::cout << "complete 3" << std::endl;

    auto combine = Task::create<ComplexData>(
        [this](Task& task) {
          auto result = task.getArtifact<ComplexData>().data();
          // std::cout << "\n  h: ";
          for(auto& x : std::span(H_, blockSize_))
          {
            // std::cout << (x) << " ";

            multiply(result, H_, task.getDependencies()[0]->getArtifact<ComplexData>().data(), blockSize_);

            /*std::cout << "\n  multipy: ";
            for(auto& x : task.getArtifact<ComplexData>())
            {
              std::cout << (x) << " ";
            }
            std::cout << "\n  add: ";
            for(auto& x : task.getDependencies()[1]->getArtifact<ComplexVec>())
            {
              std::cout << (x) << " ";
            }*/

            add(result, result, task.getDependencies()[1]->getArtifact<ComplexVec>().data(), blockSize_);

            /*std::cout << "\n  result: ";
            for(auto& x : task.getArtifact<ComplexData>())
            {
              std::cout << (x) << " ";
            }
            std::cout << std::endl;*/
          }
        },
        {input, sumUpTasks.front()},
        inverseFft_.input_.subspan(0));

    auto resultTask = Task::create<RealData>(
        [this](Task& task) {
          /*std::cout << "inverse input:\n";
          for(auto& x : inverseFft_.input_)
          {
            std::cout << (x) << " ";
          }
          std::cout << std::endl;*/
          inverseFft_.run();
          /*std::cout << "inverse output:\n";
          for(auto& x : inverseFft_.output_)
          {
            std::cout << (x) << " ";
          }
          std::cout << std::endl;*/
        },
        {combine, shift},
        inverseFft_.output_.subspan(subFilterSize_));

    return {{rootTask, resultTask}, resultTask->getArtifact<RealData>()};
  }

protected:
  static uint32_t getSubFilterSize(uint32_t inputBlockSize)
  {
    return (1 << static_cast<uint32_t>(std::ceil(std::log2(inputBlockSize) + 1))) - inputBlockSize;
  }

  void multiplyAddBlocks(uint32_t index, ComplexVec& result, uint32_t numBlocks = 1) const
  {
    /*std::cout << "Multiply Add:\n  input: ";
    for(auto& x : std::span(getDataBlock(index), blockSize_))
    {
      std::cout << (x) << " ";
    }
    std::cout << "\n  h: ";
    for(auto& x : std::span(H_ + (blockSize_ * index), blockSize_))
    {
      std::cout << (x) << " ";
    }*/

    multiply(result.data(), H_ + (blockSize_ * index), getDataBlock(index), blockSize_);

    /*std::cout << "\n  output: ";
    for(auto& x : std::span(result))
    {
      std::cout << (x) << " ";
    }
    std::cout << std::endl;*/

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
    /*std::cout << "shift\n";
    for(uint32_t i{0}; i < numBlocks_; ++i)
    {
      std::cout << "  block[" << (i) << "]:  ";
      for(uint32_t j{0}; j < blockSize_; ++j)
      {
        std::cout << (getDataBlock(i)[j]) << " ";
      }
      std::cout << std::endl;
    }
    std::cout << std::endl;*/

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

  uint32_t subFilterSize_;
  uint32_t fftSize_;
  int32_t numBlocks_;
  uint32_t blockSize_;
  std::complex<float>* H_;
  std::complex<float>* delayLine_;
  int32_t firstBlock_{0};
  BackwardFFT inverseFft_;
};