#include <gtest/gtest.h>
#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

#include "convolution.h"

using namespace std::chrono_literals;

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
      float sum = 0.0f;
      for(uint32_t j = 0; j < hrev.size(); ++j)
      {
        sum += hrev[j] * data[i + j];
      }
      result.push_back(sum);
    }

    return result;
  }

protected:
  uint32_t blockSize_{4};
  TaskRunner runner_{1};
};

TEST_F(FirFilterTest, Test_ParallelFirConvolution)
{
  std::vector<float> h{1, -1, 2, 3, 5, 9, 0, 0, 0, 0, 0, 0, 0, 0};
  std::vector<float> data{3, -1, 0, 3, 2, 0, 1, 2, 1, 8, 8, 8, 1, 2, 3, 4,
                          0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  auto result_conv = convolve(h, data);

  uint32_t blockSize = 4;

  Convolution filter(h, blockSize, 4);
  auto [inputJob, input] = Convolution::getInputTask(blockSize, 4);
  auto [rootJobs, output] = filter.getOutputTasks(inputJob, 4);

  RealVec result_upc;
  uint32_t j = 0;
  for(uint32_t k{0}; k < data.size() / 4; k++)
  {
    runner_.run(rootJobs, false);
    std::this_thread::sleep_for(500ms);
    for(auto& i : input)
    {
      i = data[j++];
    }

    runner_.run({inputJob}, false);
    std::this_thread::sleep_for(100ms);
    rootJobs[1]->execute(nullptr);

    for(auto f : output)
    {
      result_upc.push_back(f);
    }
  }

  /*ASSERT_EQ(result_upc.size(), result_conv.size());

  for(uint32_t i{0}; i < result_upc.size(); ++i)
  {
    EXPECT_NEAR(result_upc[i], result_conv[i], 0.0001f);
  }*/

  std::cout << "result:\n";
  for(auto f : result_upc)
  {
    std::cout << (f) << " ";
  }
  std::cout << std::endl;

  std::cout << "conv:\n";
  for(auto i : result_conv)
  {
    std::cout << (i) << " ";
  }
  std::cout << std::endl;
}