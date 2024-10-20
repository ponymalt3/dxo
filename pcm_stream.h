#pragma once

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <stdint.h>
#include <iostream>

template<typename SampleType>
class PcmStream
{
public:
  PcmStream(const snd_pcm_channel_area_t *area, snd_pcm_uframes_t offset)
  {
    area_ = area;
    offset_ = offset;
  }

  bool isLinear() const
  {
    return (area_[0].step / 8) == sizeof(SampleType);
  }

  uint32_t calculateOffset(uint32_t offset)
  {
    return (area_->step / (sizeof(SampleType) * 8)) * offset;
  }

//protected:
  template<typename... Args>
  void extractInterleaved(uint32_t size, Args... args)
  {
    auto *src = reinterpret_cast<SampleType*>(area_->addr);
    auto *srcMax = src + calculateOffset(size);

    while(src < srcMax)
    {
      ((*args++ = *src++), ...);
    }

    offset_ += calculateOffset(size);
  }

  template<typename... Args>
  void extractLinear(uint32_t size, Args... args)
  {
    uint32_t i = 0;
    (copy(args, reinterpret_cast<SampleType>(area_[i++].addr) + calculateOffset(offset_), size), ...);

    offset_ += calculateOffset(size);
  }

  template<typename... Args>
  void loadInterleaved(uint32_t size, Args... args)
  {
    auto *src = reinterpret_cast<SampleType*>(area_->addr);
    auto *srcMax = src + calculateOffset(size);

    while(src < srcMax)
    {
      ((*src++ = *args++), ...);
    }

    offset_ += calculateOffset(size);
  }

  template<typename... Args>
  void loadLinear(uint32_t size, Args... args)
  {
    auto *src = reinterpret_cast<SampleType*>(area_->addr);
    uint32_t i = 0;
    (copy(area_[i++].addr, args, size), ...);
    
    offset_ += calculateOffset(size);
  }

  template<typename T1, typename T2>
  void copy(T1 *dst, const T2 *src, uint32_t size)
  {
    auto srcMax = src + size;
    while(src < srcMax)
    {
      *dst++ = *src++;
    }
  }

  const snd_pcm_channel_area_t *area_;
  uint32_t offset_;
};