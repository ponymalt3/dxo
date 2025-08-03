#pragma once

#include <stdarg.h>

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
    kNumOutputChannels = 8
  };

  AlsaPluginDxO(const std::string& path, uint32_t blockSize)
      : blockSize_(blockSize),
        inputs_(3),
        outputs_(7),
        inputOffset_(0),
        outputBuffer_{new int16_t[blockSize_ * kNumOutputChannels]}
  {
    memset(this, 0, sizeof(snd_pcm_ioplug_t));

    auto coeffs = loadFIRCoeffs(path);
    assert(coeffs.size() == 7 && "Coeffs file need to provide 7 FIR transfer functions");

    std::vector<FirMultiChannelCrossover::ConfigType> config{{0, coeffs[0]},
                                                             {0, coeffs[1]},
                                                             {0, coeffs[2]},
                                                             {1, coeffs[3]},
                                                             {1, coeffs[4]},
                                                             {1, coeffs[5]},
                                                             {2, coeffs[6]}};

    crossover_ = std::make_unique<FirMultiChannelCrossover>(blockSize_, 3, config, 3);

    for(auto i{0}; i < inputs_.size(); ++i)
    {
      inputs_[i] = crossover_->getInputBuffer(i).data();
    }

    for(auto i{0}; i < outputs_.size(); ++i)
    {
      outputs_[i] = crossover_->getOutputBuffer(i).data();
    }
  }

  ~AlsaPluginDxO() {}

  static std::vector<std::vector<float>> loadFIRCoeffs(const std::string& path)
  {
    std::ifstream file(path);

    if(!file)
    {
      throw std::invalid_argument("Error: loadFIRCoeffs with " + path + " failed!");
    }

    std::vector<std::vector<float>> filters;
    while(!file.eof())
    {
      std::string line;
      while(std::getline(file, line))
      {
        uint32_t posNotWhiteSpace = 0;
        while(std::isspace(line[posNotWhiteSpace]))
        {
          ++posNotWhiteSpace;
        }

        if(line.substr(posNotWhiteSpace).starts_with('#') || line.length() == posNotWhiteSpace)
        {
          continue;
        }

        std::vector<float> coeffs;
        std::istringstream iss(line);
        while(!iss.eof())
        {
          double value = 0;
          iss >> value;
          coeffs.push_back(static_cast<float>(value));
        }

        if(coeffs.size() > 0)
        {
          filters.push_back(coeffs);
        }
      }
    }

    return filters;
  }

  template <typename... Args>
  void print(const char* fmt, Args... args)
  {
    snd_output_printf(output_, fmt, args...);
    // snd_output_flush(output_);
  }

  template <typename _InputSampleType>
  uint32_t update(PcmStream<_InputSampleType>& src, uint32_t size, bool hasLFE)
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
      streamPos_ += segmentSize;

      if(inputOffset_ == blockSize_)
      {
        auto start = std::chrono::high_resolution_clock::now();
        for(uint32_t i = 0; i < blockSize_; ++i)
        {
        }
        memcpy(outputs_[0], inputs_[0], sizeof(float) * blockSize_);
        memcpy(outputs_[1], inputs_[1], sizeof(float) * blockSize_);
        memcpy(outputs_[2], inputs_[0], sizeof(float) * blockSize_);
        memcpy(outputs_[3], inputs_[1], sizeof(float) * blockSize_);
        memcpy(outputs_[4], inputs_[0], sizeof(float) * blockSize_);
        memcpy(outputs_[5], inputs_[1], sizeof(float) * blockSize_);
        memcpy(outputs_[6], inputs_[2], sizeof(float) * blockSize_);

        // crossover_->updateInputs();
        auto end = std::chrono::high_resolution_clock::now();

        double time_taken = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() * 1e-9;
        totalTime_ += time_taken;
        ++totalBlocks_;

        PcmStream<int16_t> dst(outputBuffer_.get(), kNumOutputChannels);
        dst.loadInterleaved(blockSize_,
                            outputs_[0],
                            outputs_[1],
                            outputs_[2],
                            outputs_[3],
                            outputs_[0],  // unused
                            outputs_[6],
                            outputs_[4],
                            outputs_[5]);

        auto result = snd_pcm_writei(pcm_output_device_, outputBuffer_.get(), blockSize_);

        if(result != blockSize_)
        {
          if(result < 0)
          {
            snd_output_printf(output_, "write error [%s]\n", snd_strerror(result));
          }
          else
          {
            snd_output_printf(output_, "incomplete write %ld/%d\n", result, blockSize_);
          }

          snd_pcm_recover(pcm_output_device_, result, 0);
        }

        inputOffset_ = 0;
      }
    }
    return 0;
  }

  uint32_t blockSize_{};
  std::vector<float*> inputs_{nullptr};
  std::vector<float*> outputs_{nullptr};
  uint32_t inputOffset_{0};
  snd_output_t* output_{nullptr};
  std::unique_ptr<FirMultiChannelCrossover> crossover_;
  snd_pcm_t* pcm_output_device_{nullptr};
  snd_pcm_hw_params_t* params_{nullptr};
  std::string pcmName_{};
  std::atomic<uint32_t> streamPos_{0};
  std::unique_ptr<int16_t> outputBuffer_;
  double totalTime_{0};
  uint32_t totalBlocks_{0};
};
