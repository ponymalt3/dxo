#include <gtest/gtest.h>
#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "convolution.h"
#include "fir_crossover.h"

class FirFilterTest : public testing::Test
{
public:
  std::vector<float> convolve(const std::span<float>& h, const std::span<float>& d)
  {
    std::vector<float> hrev(h.rbegin(), h.rend());
    std::vector<float> data(h.size() - 1, 0.0f);
    data.insert(data.end(), d.begin(), d.end());

    std::vector<float> result;
    for(uint32_t i{0}; i < data.size() - hrev.size() + 1; ++i)
    {
      double sum = 0.0;
      for(uint32_t j = 0; j < hrev.size(); ++j)
      {
        sum += hrev[j] * data[i + j];
      }
      result.push_back(static_cast<float>(sum));
    }

    return result;
  }

  void expectEqual(const std::span<float>& a, const std::span<float>& b)
  {
    EXPECT_GT(std::min(a.size(), b.size()), 1);

    constexpr auto epsilon = 0.03f;

    for(auto i{0}; i < std::min(a.size(), b.size()); ++i)
    {
      auto dif{std::fabs(a[i] - b[i])};
      auto epsilonScaled = epsilon;
      if(std::abs(a[i]) > 0.0000001 && std::abs(b[i]) > 0.0000001)
      {
        epsilonScaled = epsilon * std::max(std::fabs(a[i]), std::fabs(b[i]));
      }
      else
      {
        epsilonScaled = 0.00001f;
      }

      EXPECT_LE(dif, epsilonScaled) << " at " << (i) << "\n  e = " << (epsilonScaled) << "\n  a = " << (a[i])
                                    << "\n  b = " << (b[i]);
    }
  }

protected:
  uint32_t blockSize_{4};
  TaskRunner runner_{1};
};

TEST_F(FirFilterTest, Test_ParallelConvolution)
{
  std::vector<float> h{-1.14, -0.08, 1.49, -0.79, -1.38, -4.73, 1.9, -4.41, 2.63, 4.26};
  std::vector<float> data{3, -1, 0, 3, 2, 0, 1, 2, 1, 8, 8, 8, 1, 2, 3, 4, 0, 0, 0, 0,
                          0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  for(auto blockSize : {2, 3, 4, 5})
  {
    auto result_conv = convolve(h, data);

    Convolution filter(h, blockSize);
    auto [inputJob, input] = Convolution::getInputTask(blockSize);
    auto [rootJobs, output] = filter.getOutputTasks(inputJob, 1);

    RealVec result_upc;
    uint32_t j = 0;
    for(uint32_t k{0}; k < data.size() / blockSize; k++)
    {
      runner_.run(rootJobs, false);
      for(auto& i : input)
      {
        i = data[j++];
      }

      runner_.run({inputJob});

      for(auto f : output)
      {
        result_upc.push_back(f);
      }
    }

    expectEqual(result_upc, result_conv);
  }
}

TEST_F(FirFilterTest, Test_FirMultiChannelCrossover)
{
  constexpr auto BlockSize = 120U;
  constexpr auto NumBlocks = 59U;
  constexpr auto NumOutputs = 6U;
  constexpr auto NumInputs = 3U;

  std::vector<std::vector<float>> h;
  for(size_t size : {253, 170, 131, 1023, 721, 445})
  {
    std::vector<float> rnd(size);
    for(auto& r : rnd)
    {
      r = float((std::rand() % 1000) - 500) / 100;
    }
    h.push_back(rnd);
  }

  std::vector<std::vector<float>> inputs(NumInputs);
  for(auto& ch : inputs)
  {
    for(auto i{0}; i < BlockSize * NumBlocks; ++i)
    {
      ch.push_back(float((std::rand() % 10000) - 5000) / 100);
    }
  }

  std::vector<std::vector<float>> outputs(NumOutputs);

  std::vector<FirMultiChannelCrossover::ConfigType> config;
  for(auto i{0}; i < NumOutputs; ++i)
  {
    config.push_back({i % NumInputs, h[i]});
  }

  FirMultiChannelCrossover fmcc(BlockSize, NumInputs, config, 3);

  std::vector<decltype(inputs[0].begin())> inputIterators;
  for(auto& ch : inputs)
  {
    inputIterators.push_back(ch.begin());
  }

  for(auto i{0}; i < NumBlocks; ++i)
  {
    for(auto j{0}; j < inputIterators.size(); ++j)
    {
      for(auto& r : fmcc.getInputBuffer(j))
      {
        r = *inputIterators[j]++;
      }
    }

    fmcc.updateInputs();

    for(auto i{0}; i < h.size(); ++i)
    {
      auto output = fmcc.getOutputBuffer(i);
      outputs[i].insert(outputs[i].end(), output.begin(), output.end());
    }
  }

  const std::vector<uint32_t> InputChannelMap{0, 1, 2, 0, 1, 2};

  for(auto i{0U}; i < h.size(); ++i)
  {
    auto conv = convolve(h[i], inputs[InputChannelMap[i]]);
    expectEqual(std::span(conv).subspan(0, BlockSize * NumBlocks),
                std::span(outputs[i]).subspan(0, BlockSize * NumBlocks));
  }
}
