#pragma once

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <stdint.h>

#include <cmath>
#include <type_traits>

template <typename DstType, typename SrcType>
inline DstType convert(SrcType sample)
{
  DstType new_sample{sample};
  return new_sample;
}

template <>
inline float convert<float, int16_t>(int16_t sample)
{
  return std::ldexp(static_cast<float>(sample), -16);
}

template <typename SampleType>
class PcmStream
{
public:
  PcmStream(SampleType* data, uint32_t step)
  {
    addr_ = data;
    step_ = step;
  }

  PcmStream(const snd_pcm_channel_area_t* area, snd_pcm_uframes_t offset)
  {
    addr_ = reinterpret_cast<SampleType*>(snd_pcm_channel_area_addr(area, offset));
    step_ = snd_pcm_channel_area_step(area) / sizeof(SampleType);

    assert((addr_ + 1) == snd_pcm_channel_area_addr(area + 1, offset) &&
           "Channels must be stored interleaved");
  }

  template <typename... Args>
  void extractInterleaved(uint32_t size, Args... args)
  {
    auto* src = addr_;
    auto* srcMax = src + calculateOffset(size);

    while(src < srcMax)
    {
      ((*args++ = convert<std::remove_reference_t<decltype(*args)>>(*src++)), ...);
    }

    addr_ = srcMax;
  }

  template <typename... Args>
  void loadInterleaved(uint32_t size, Args... args)
  {
    auto* src = addr_;
    auto* srcMax = src + calculateOffset(size);

    while(src < srcMax)
    {
      ((*src++ = convert<std::remove_reference_t<decltype(*src)>>(*args++)), ...);
    }

    addr_ = srcMax;
  }

  // protected:
  uint32_t calculateOffset(uint32_t offset) { return step_ * offset; }

  SampleType* addr_;
  uint32_t step_;
};

template <typename SampleType>
class PcmBuffer : public PcmStream<SampleType>
{
public:
  PcmBuffer(uint32_t maxSize)
      : PcmStream<SampleType>(reinterpret_cast<SampleType*>(0), 0),
        buffer_{new SampleType[maxSize]},
        size_{0},
        maxSize_{maxSize}
  {
  }

  void store(const PcmStream<SampleType>& stream, uint32_t size)
  {
    memcpy(
        buffer_.get(), stream.addr_, std::min<uint32_t>(maxSize_, stream.step_ * size * sizeof(SampleType)));
    this->step_ = stream.step_;
    this->addr_ = buffer_.get();
    size_ = size;
  }

  uint32_t getNumChannels() const { return this->step_ / sizeof(SampleType); }

  uint32_t available() const { return size_ - ((this->addr_ - buffer_.get()) / this->step_); }

protected:
  std::unique_ptr<SampleType> buffer_;
  uint32_t size_;
  uint32_t maxSize_;
};