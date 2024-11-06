#pragma once

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <stdint.h>

template <typename SampleType>
class PcmStream
{
public:
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
      ((*args++ = *src++), ...);
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
      ((*src++ = *args++), ...);
    }

    addr_ = srcMax;
  }

protected:
  uint32_t calculateOffset(uint32_t offset) { return step_ * offset; }

  const snd_pcm_channel_area_t* area_;
  SampleType* addr_;
  uint32_t step_;
  uint32_t offset_;
};