#include <gtest/gtest.h>

#include <filesystem>

#include "alsa_plugin.h"
#include "pcm_stream.h"

class AlsaPluginTest : public testing::Test
{
public:
  static void SetUpTestSuite()
  {
    std::filesystem::current_path(std::filesystem::current_path().parent_path() / "rpi_digital_crossover");
  }

  void sleepFor(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

  template <typename T>
  std::vector<test_helper::Channel<T>> GetInterleavedData(uint32_t channels = 2, uint32_t step_offset = 0)
  {
    mem_.push_back(std::make_unique<unsigned char[]>(256 * 4 * sizeof(T)));
    auto* typed_buffer = reinterpret_cast<T*>(mem_.back().get());
    std::vector<test_helper::Channel<T>> result{};
    for(auto i{0}; i < channels; ++i)
    {
      result.push_back(test_helper::Channel<T>(typed_buffer + i, 256, channels + step_offset));
    }
    return result;
  }

  AlsaPluginDxO plugin{"coeffs_reduced.m", 256, "", nullptr};
  std::vector<std::unique_ptr<unsigned char[]>> mem_;
};

TEST_F(AlsaPluginTest, Test_LoadCoefficents)
{
  auto coeffs = plugin.loadFIRCoeffs("coeffs.m", 123.456f);
  ASSERT_EQ(coeffs.size(), 7);

  for(auto& filter : coeffs)
  {
    EXPECT_EQ(4096, filter.size());
    EXPECT_EQ(filter[0], 123.456f);
  }
}

TEST_F(AlsaPluginTest, Test_PluginUpdate)
{
  static constexpr auto kFrames = 256;
  auto interleaved = GetInterleavedData<float>(2);
  for(auto i{0}; i < kFrames; ++i)
  {
    interleaved[0].setData(i, {1.0f / static_cast<float>(i + 1)});
    interleaved[1].setData(i, {-1.0f / static_cast<float>(i + 1)});
  }
  PcmStream<float> stream(interleaved.data(), 0);

  const auto test_writer = [&interleaved](const int16_t* data, uint32_t frames) {
    ASSERT_EQ(frames, kFrames);
    auto ch0 = interleaved.data()[0].getData(0, frames);
    auto ch1 = interleaved.data()[1].getData(0, frames);

    for(auto i{0}; i < kFrames; ++i)
    {
      auto index = i * AlsaPluginDxO::kNumOutputChannels;
      EXPECT_NEAR(data[index + 0], ch0[i] * 32768.0f, 1.1f);
      EXPECT_NEAR(data[index + 1], ch1[i] * 32768.0f, 1.1f);
      EXPECT_NEAR(data[index + 2], ch0[i] * 32768.0f, 1.1f);
      EXPECT_NEAR(data[index + 3], ch1[i] * 32768.0f, 1.1f);
      EXPECT_NEAR(data[index + 6], ch0[i] * 32768.0f, 1.1f);
      EXPECT_NEAR(data[index + 7], ch1[i] * 32768.0f, 1.1f);
      EXPECT_NEAR(data[index + 5], 0.0f, 1.1f);
    }
  };

  plugin.update(stream, kFrames, false, test_writer);
}
