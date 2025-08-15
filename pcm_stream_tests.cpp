#include <gtest/gtest.h>

#include "pcm_stream.h"

class PcmStreamTest : public testing::Test
{
protected:
  template <typename T>
  std::vector<test_helper::Channel<T>> GetInterleavedData()
  {
    auto* typed_buffer = reinterpret_cast<T*>(buffer);
    return {test_helper::Channel<T>(typed_buffer + 0, 256, 8),
            test_helper::Channel<T>(typed_buffer + 1, 256, 8),
            test_helper::Channel<T>(typed_buffer + 2, 256, 8),
            test_helper::Channel<T>(typed_buffer + 3, 256, 8),
            test_helper::Channel<T>(typed_buffer + 4, 256, 8),
            test_helper::Channel<T>(typed_buffer + 5, 256, 8),
            test_helper::Channel<T>(typed_buffer + 6, 256, 8),
            test_helper::Channel<T>(typed_buffer + 7, 256, 8)};
  }

  uint8_t buffer[4 * 8 * 256];  // 8192
};

TEST_F(PcmStreamTest, Test_InterleavedExtract)
{
  auto interleaved = GetInterleavedData<int32_t>();

  float ch1[16], ch2[16], ch3[16], ch4[16], ch5[16], ch6[16], ch7[16], ch8[16];

  int32_t i = 0;
  for(auto& c : interleaved)
  {
    c.setData(0, {i++, i++, i++, i++});
  }

  PcmStream<int32_t> stream(interleaved.data(), 0);
  stream.extractInterleaved(4U, ch1, ch2, ch3, ch4, ch5, ch6, ch7, ch8);

  for(auto i{0}; i < 4; ++i)
  {
    EXPECT_EQ(ch1[i], 0 + i);
    EXPECT_EQ(ch2[i], 4 + i);
    EXPECT_EQ(ch3[i], 8 + i);
    EXPECT_EQ(ch4[i], 12 + i);
    EXPECT_EQ(ch5[i], 16 + i);
    EXPECT_EQ(ch6[i], 20 + i);
    EXPECT_EQ(ch7[i], 24 + i);
    EXPECT_EQ(ch8[i], 28 + i);
  }
}

TEST_F(PcmStreamTest, Test_InterleavedExtractScaling)
{
  constexpr auto value = 17;

  {
    auto interleaved = GetInterleavedData<int32_t>();
    interleaved[0].setData(0, {value});
    PcmStream<int32_t> stream(interleaved.data(), 0);
    float extract;
    stream.extractInterleaved(1U, &extract);
    EXPECT_FLOAT_EQ(extract, static_cast<float>(value));
  }

  {
    auto interleaved = GetInterleavedData<int16_t>();
    interleaved[0].setData(0, {value});
    PcmStream<int16_t> stream(interleaved.data(), 0);
    float extract;
    stream.extractInterleaved(1U, &extract);
    EXPECT_EQ(extract, static_cast<float>(value) / 32768);
  }

  {
    auto interleaved = GetInterleavedData<float>();
    interleaved[0].setData(0, {value});
    PcmStream<float> stream(interleaved.data(), 0);
    float extract;
    stream.extractInterleaved(1U, &extract);
    EXPECT_EQ(extract, value);
  }
}

TEST_F(PcmStreamTest, Test_InterleavedLoad)
{
  auto interleaved = GetInterleavedData<int32_t>();

  float ch1[3] = {1.0f, 2.0f, 3.0f};
  float ch2[3] = {4.0f, 5.0f, 6.0f};
  float ch3[3] = {7.0f, 8.0f, 9.0f};
  float ch4[3] = {10.0f, 11.0f, 12.0f};
  float ch5[3] = {13.0f, 14.0f, 15.0f};
  float ch6[3] = {16.0f, 17.0f, 18.0f};
  float ch7[3] = {19.0f, 20.0f, 21.0f};
  float ch8[3] = {22.0f, 23.0f, 24.0f};

  PcmStream<int32_t> stream(interleaved.data(), 0);
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

TEST_F(PcmStreamTest, Test_PcmBuffer)
{
  auto interleaved = GetInterleavedData<int32_t>();

  float ch1[8][4];
  float ch2[8][4];

  int32_t i = 0;
  for(auto& c : interleaved)
  {
    c.setData(0, {i++, i++, i++, i++});
  }

  PcmStream<int32_t> stream(interleaved.data(), 0);
  PcmBuffer<int32_t> buffer(256);
  buffer.store(stream, 4);

  stream.extractInterleaved(4U, ch1[0], ch1[1], ch1[2], ch1[3], ch1[4], ch1[5], ch1[6], ch1[7]);
  buffer.extractInterleaved(4U, ch2[0], ch2[1], ch2[2], ch2[3], ch2[4], ch2[5], ch2[6], ch2[7]);

  for(auto i{0}; i < 8; ++i)
  {
    bool equal{true};
    for(auto j{0}; j < 4; ++j)
    {
      equal = equal && ch1[i][j] == ch2[i][j];
    }
    EXPECT_TRUE(equal) << " at block " << (i);
  }
}
