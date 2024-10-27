#include <gtest/gtest.h>
#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

#include "convolution.h"

class FirFilterTest : public testing::Test
{
public:
  std::vector<float> convolve(const std::vector<float>& h, std::vector<float> data)
  {
    auto hrev = h;
    std::reverse(hrev.begin(), hrev.end());

    std::vector<float> fillZeros(h.size() - 1, 0.0f);
    data.insert(data.begin(), fillZeros.begin(), fillZeros.end());

    std::vector<float> result;
    for(uint32_t i{0}; i < data.size() - h.size() + 1; ++i)
    {
      float sum = 0.0f;
      for(uint32_t j = 0; j < h.size(); ++j)
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
  std::vector<float> h{1.0f, -1.0f, 2.0f, 3, 5, 9};  //,

  std::vector<float> data{3,    -1,   0,    3,    2,    0,    1,    2,    1,    8,    8,
                          8,    1,    2,    3,    4,    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0};

  auto result_conv = convolve(h, data);

  Convolution filter(h, 3, 4);

  auto [inputJob, input] = filter.getInputTask();

  auto [root, result] = filter.getOutputTasks(inputJob, 1);

  RealVec results;
  uint32_t j = 0;
  for(uint32_t k{0}; k < 2; k++)
  {
    std::cout << "######################### RUN ##########################" << std::endl;
    runner_.run(root, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    for(auto& i : input)
    {
      i = data[j++];
    }
    std::cout << "Bla:\n";
    for(auto& i : input)
    {
      std::cout << (i) << " ";
    }
    std::cout << std::endl;
    runner_.run({inputJob}, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    root[1]->execute(nullptr);

    std::cout << "final done " << (result.size()) << std::endl;

    for(auto f : result)
    {
      results.push_back(f);
    }
  }
  std::cout << "result::\n";
  for(auto f : results)
  {
    std::cout << (f / 6) << " ";
  }
  std::cout << std::endl;

  std::cout << "conv:\n";
  for(auto i : result_conv)
  {
    std::cout << (i) << " ";
  }
  std::cout << std::endl;
}