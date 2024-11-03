#include <gtest/gtest.h>

#include <initializer_list>
#include <iostream>
#include <vector>

#include "pcm_stream.h"

template <typename SampleType>
class Channel : public snd_pcm_channel_area_t
{
public:
  Channel(SampleType* addr, uint32_t size, uint32_t step)
  {
    this->addr = addr;
    this->step = sizeof(SampleType) * 8 * step;
  }

  void setData(uint32_t offset, std::initializer_list<SampleType> data)
  {
    for(auto d : data)
    {
      reinterpret_cast<SampleType*>(addr)[offset * step / (sizeof(SampleType) * 8)] = d;
      // std::cout<<"set "<<((void*)(reinterpret_cast<SampleType*>(addr)+(offset * step / (sizeof(SampleType)
      // * 8))))<<" to "<<(d)<<std::endl;
      ++offset;
    }
  }

  std::vector<SampleType> getData(uint32_t offset, uint32_t size)
  {
    std::vector<SampleType> data;
    while(data.size() < size)
    {
      data.push_back(reinterpret_cast<SampleType*>(addr)[offset * step / (sizeof(SampleType) * 8)]);
      ++offset;
    }

    return data;
  }
};

class PcmStreamTest : public testing::Test
{
protected:
  PcmStreamTest()
  {
    buffer1 = new int16_t[3 * 256];
    buffer2 = new int32_t[8 * 256];

    linear = {Channel<int16_t>(buffer1 + 0, 256, 1),
              Channel<int16_t>(buffer1 + 256, 256, 1),
              Channel<int16_t>(buffer1 + 512, 256, 1)};

    interleaved = {Channel<int32_t>(buffer2 + 0, 256, 8),
                   Channel<int32_t>(buffer2 + 1, 256, 8),
                   Channel<int32_t>(buffer2 + 2, 256, 8),
                   Channel<int32_t>(buffer2 + 3, 256, 8),
                   Channel<int32_t>(buffer2 + 4, 256, 8),
                   Channel<int32_t>(buffer2 + 5, 256, 8),
                   Channel<int32_t>(buffer2 + 6, 256, 8),
                   Channel<int32_t>(buffer2 + 7, 256, 8)};
  }

  ~PcmStreamTest()
  {
    linear.clear();
    interleaved.clear();

    delete[] buffer1;
    delete[] buffer2;
  }

  int16_t* buffer1;
  int32_t* buffer2;
  std::vector<Channel<int16_t>> linear;
  std::vector<Channel<int32_t>> interleaved;
};

TEST_F(PcmStreamTest, Test_InterleavedExtract)
{
  float ch1[16], ch2[16], ch3[16], ch4[16], ch5[16], ch6[16], ch7[16], ch8[16];

  int32_t i = 0;
  for(auto& c : interleaved)
  {
    c.setData(0, {i++, i++, i++, i++});
  }

  PcmStream<int32_t> stream(&interleaved[0], 0);
  stream.extractInterleaved(4U, ch1, ch2, ch3, ch4, ch5, ch6, ch7, ch8);

  EXPECT_EQ(ch1[0], 0);
  EXPECT_EQ(ch2[0], 4);
  EXPECT_EQ(ch3[0], 8);
  EXPECT_EQ(ch4[0], 12);
  EXPECT_EQ(ch5[0], 16);
  EXPECT_EQ(ch6[0], 20);
  EXPECT_EQ(ch7[0], 24);
  EXPECT_EQ(ch8[0], 28);
}

TEST_F(PcmStreamTest, Test_InterleavedLoad)
{
  float ch1[3] = {1.0f, 2.0f, 3.0f};
  float ch2[3] = {4.0f, 5.0f, 6.0f};
  float ch3[3] = {7.0f, 8.0f, 9.0f};
  float ch4[3] = {10.0f, 11.0f, 12.0f};
  float ch5[3] = {13.0f, 14.0f, 15.0f};
  float ch6[3] = {16.0f, 17.0f, 18.0f};
  float ch7[3] = {19.0f, 20.0f, 21.0f};
  float ch8[3] = {22.0f, 23.0f, 24.0f};

  PcmStream<int32_t> stream(&interleaved[0], 0);
  stream.loadInterleaved(3U, ch1, ch2, ch3, ch4, ch5, ch6, ch7, ch8);

  EXPECT_EQ(interleaved[0].getData(0, 3), std::vector<int32_t>({1, 2, 3}));
  EXPECT_EQ(interleaved[1].getData(0, 3), std::vector<int32_t>({4, 5, 6}));
  EXPECT_EQ(interleaved[2].getData(0, 3), std::vector<int32_t>({7, 8, 9}));
  EXPECT_EQ(interleaved[3].getData(0, 3), std::vector<int32_t>({10, 11, 12}));
  EXPECT_EQ(interleaved[4].getData(0, 3), std::vector<int32_t>({13, 14, 15}));
  EXPECT_EQ(interleaved[5].getData(0, 3), std::vector<int32_t>({16, 17, 18}));
  EXPECT_EQ(interleaved[6].getData(0, 3), std::vector<int32_t>({19, 20, 21}));
  EXPECT_EQ(interleaved[7].getData(0, 3), std::vector<int32_t>({22, 23, 24}));
}