#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <initializer_list>
#include <thread>
#include <vector>

#include "alsa_plugin.h"
#include "pcm_stream.h"

template <typename SampleType>
class Channel : public snd_pcm_channel_area_t
{
public:
  Channel(SampleType* addr, uint32_t size, uint32_t step)
  {
    this->addr = addr;
    this->step = step * sizeof(SampleType) * 8;
    this->first = 0;
  }

  void setData(uint32_t offset, std::initializer_list<SampleType> data)
  {
    for(auto d : data)
    {
      reinterpret_cast<SampleType*>(addr)[offset * step / (sizeof(SampleType) * 8)] = d;
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
    /*linear = {Channel<int16_t>(buffer1 + 0, 256, 1),
              Channel<int16_t>(buffer1 + 256, 256, 1),
              Channel<int16_t>(buffer1 + 512, 256, 1)};*/
  }

  template <typename T>
  std::vector<Channel<T>> GetInterleavedData()
  {
    auto* typed_buffer = reinterpret_cast<T*>(buffer);
    return {Channel<T>(typed_buffer + 0, 256, 8),
            Channel<T>(typed_buffer + 1, 256, 8),
            Channel<T>(typed_buffer + 2, 256, 8),
            Channel<T>(typed_buffer + 3, 256, 8),
            Channel<T>(typed_buffer + 4, 256, 8),
            Channel<T>(typed_buffer + 5, 256, 8),
            Channel<T>(typed_buffer + 6, 256, 8),
            Channel<T>(typed_buffer + 7, 256, 8)};
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

  EXPECT_EQ(ch1[0], 0);
  EXPECT_EQ(ch2[0], 4);
  EXPECT_EQ(ch3[0], 8);
  EXPECT_EQ(ch4[0], 12);
  EXPECT_EQ(ch5[0], 16);
  EXPECT_EQ(ch6[0], 20);
  EXPECT_EQ(ch7[0], 24);
  EXPECT_EQ(ch8[0], 28);
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

class AlsaPluginTest : public testing::Test
{
public:
  void sleepFor(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

  template <typename T>
  std::vector<Channel<T>> GetInterleavedData(uint32_t channels = 2, uint32_t step_offset = 0)
  {
    mem_.push_back(std::make_unique<unsigned char[]>(256 * 4 * sizeof(T)));
    auto* typed_buffer = reinterpret_cast<T*>(mem_.back().get());
    std::vector<Channel<T>> result{};
    for(auto i{0}; i < channels; ++i)
    {
      result.push_back(Channel<T>(typed_buffer + i, 256, channels + step_offset));
    }
    return result;
  }

  AlsaPluginDxO plugin{"coeffs_reduced", 256};
  std::vector<std::unique_ptr<unsigned char[]>> mem_;
};

TEST_F(AlsaPluginTest, Test_LoadCoefficents)
{
  auto coeffs = AlsaPluginDxO::loadFIRCoeffs("coeffs.m");
  ASSERT_EQ(coeffs.size(), 7);

  for(auto& filter : coeffs)
  {
    EXPECT_EQ(4096, filter.size());
  }
}

TEST_F(AlsaPluginTest, Test_PluginUpdate)
{
  auto interleaved = GetInterleavedData<float>(2);
  for(auto i{0}; i < 256; ++i)
  {
    interleaved[0].setData(i, {static_cast<float>(i)});
    interleaved[1].setData(i, {static_cast<float>(i) + 256});
  }
  PcmStream<float> stream(interleaved.data(), 0);

  const auto test_writer = [&interleaved](const int16_t* data, uint32_t frames) {
    ASSERT_EQ(frames, 256);
    auto ch0 = interleaved.data()[0].getData(0, frames);
    auto ch1 = interleaved.data()[1].getData(0, frames);

    for(auto i{0}; i < frames; ++i)
    {
      auto index = i * AlsaPluginDxO::kNumOutputChannels;
      EXPECT_EQ(data[index + 0], static_cast<int16_t>(ch0[i] * 32768));
      EXPECT_EQ(data[index + 1], static_cast<int16_t>(ch1[i] * 32768));
    }
  };

  plugin.update(stream, 256, false, test_writer);
}
