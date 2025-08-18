#pragma once

#include <stdarg.h>
#include <stdint.h>

#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "crossover/fir_crossover.h"
#include "fftw3.h"
#include "pcm_stream.h"

class AlsaPluginDxO : public snd_pcm_ioplug_t
{
public:
  enum
  {
    kNumOutputChannels = 8,
    kScaleS16LE = 32767,
    kChFL = 0,
    kChFR = 3,
    kChRL = 1,
    kChRR = 4,
    kChSL = 2,
    kChSR = 5,
    kChLFE = 6,
    kChUnknown = 0
  };
  static constexpr std::array<uint32_t, 20> kMapAlsaChannel{
      kChUnknown, kChUnknown, kChUnknown, kChFL,      kChFR,      kChRL,     kChRR,
      kChUnknown, kChLFE,     kChSL,      kChSR,      kChUnknown, kChSL,     kChSR,
      kChSL,      kChSR,      kChUnknown, kChUnknown, kChUnknown, kChUnknown};

  AlsaPluginDxO(const std::string& path,
                uint32_t blockSize,
                const std::string slavePcm,
                const snd_pcm_ioplug_callback_t* callbacks);

  std::vector<std::vector<float>> loadFIRCoeffs(const std::string& path, float scale);
  void enableLogging();
  bool writePcm(const int16_t* data, const uint32_t frames);

  template <typename... Args>
  void print(Args... args)
  {
    (logging_ << ... << args) << "\n";
  }

  template <typename _InputSampleType, typename _LambdaType>
  uint32_t update(PcmStream<_InputSampleType>& src, uint32_t size, bool hasLFE, _LambdaType writer)
  {
    auto i{0U};
    while(i < size)
    {
      uint32_t segmentSize = std::min(size - i, blockSize_ - inputOffset_);

      if(hasLFE)
      {
        src.extractInterleaved(
            segmentSize, inputs_[0] + inputOffset_, inputs_[1] + inputOffset_, inputs_[2] + inputOffset_);
      }
      else
      {
        src.extractInterleaved(segmentSize, inputs_[0] + inputOffset_, inputs_[1] + inputOffset_);

        for(auto j{inputOffset_}; j < inputOffset_ + segmentSize; ++j)
        {
          inputs_[2][j] = (inputs_[0][j] + inputs_[1][j]) / 2;
        }
      }

      inputOffset_ += segmentSize;
      i += segmentSize;
      // streamPos_ += segmentSize;

      if(inputOffset_ == blockSize_)
      {
        auto start = std::chrono::high_resolution_clock::now();
        crossover_->updateInputs();
        auto end = std::chrono::high_resolution_clock::now();

        double time_taken = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() * 1e-9;
        totalTime_ += time_taken;
        ++totalBlocks_;

        PcmStream<int16_t> dst(outputBuffer_.get(), kNumOutputChannels);
        dst.loadInterleaved(blockSize_,
                            outputs_[channelMap_[0]],
                            outputs_[channelMap_[1]],
                            outputs_[channelMap_[2]],
                            outputs_[channelMap_[3]],
                            outputs_[channelMap_[4]],  // unused
                            outputs_[channelMap_[5]],
                            outputs_[channelMap_[6]],
                            outputs_[channelMap_[7]]);

        writer(outputBuffer_.get(), blockSize_);
        streamPos_ += blockSize_;
        inputOffset_ = 0;
      }
    }

    return 0;
  }

  // ALSA ioplug functions
  static snd_pcm_sframes_t dxo_pointer(snd_pcm_ioplug_t* io);
  static snd_pcm_sframes_t dxo_transfer(snd_pcm_ioplug_t* io,
                                        const snd_pcm_channel_area_t* src_areas,
                                        snd_pcm_uframes_t src_offset,
                                        snd_pcm_uframes_t size);
  static int dxo_try_open_device(AlsaPluginDxO* plugin);
  static int dxo_prepare(snd_pcm_ioplug_t* io);
  static int dxo_close(snd_pcm_ioplug_t* io);
  static snd_pcm_chmap_query_t** dxo_query_chmaps(snd_pcm_ioplug_t* io);
  static snd_pcm_chmap_t* dxo_get_chmap(snd_pcm_ioplug_t* io);
  static int dxo_hw_params(snd_pcm_ioplug_t* io, snd_pcm_hw_params_t* params);
  static int dxo_delay(snd_pcm_ioplug_t* io, snd_pcm_sframes_t* delayp);

protected:
  uint32_t blockSize_{};
  std::vector<float*> inputs_{nullptr};
  std::vector<float*> outputs_{nullptr};
  uint32_t inputOffset_{0};
  std::ofstream logging_{};
  std::unique_ptr<FirMultiChannelCrossover> crossover_;
  snd_pcm_t* pcm_output_device_{nullptr};
  std::string pcmName_{};
  std::atomic<uint32_t> streamPos_{0};
  std::unique_ptr<int16_t> outputBuffer_;
  std::array<uint32_t, 8> channelMap_{kChFL, kChFR, kChRL, kChRR, kChUnknown, kChLFE, kChSL, kChSR};
  double totalTime_{0};
  uint32_t totalBlocks_{0};
};
