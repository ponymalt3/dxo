#pragma once

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "crossover/fir_crossover.h"
#include "fftw3.h"
#include "pcm_stream.h"

template <typename _T>
class RingBuffer
{
public:
  RingBuffer(uint32_t numElements, _T init) : elements_(numElements, init), stop_(false) {}

  ~RingBuffer()
  {
    stop_ = true;
    cv_.notify_all();
  }

  _T& getElementForWrite()
  {
    waitNotFull();
    return elements_[wptr_ % elements_.size()];
  }

  void writeComplete()
  {
    waitNotFull();
    ++wptr_;
    cv_.notify_one();
  }

  _T& getElementForRead()
  {
    waitNotEmpty();
    return elements_[rptr_ % elements_.size()];
  }

  void readComplete()
  {
    waitNotEmpty();
    ++rptr_;
    cv_.notify_one();
  }

  bool empty() const { return rptr_ == wptr_; }
  bool full() const { return (wptr_ - rptr_) == elements_.size() - 1; }

  void waitNotFull()
  {
    if(full())
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return !full() || stop_; });
    }
  }

  void waitNotEmpty()
  {
    if(empty())
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return !empty() || stop_; });
    }
  }

protected:
  std::vector<_T> elements_;
  std::atomic<uint32_t> rptr_{0};
  std::atomic<uint32_t> wptr_{0};
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic_bool stop_;
};

class AlsaPluginDxO : public snd_pcm_ioplug_t
{
public:
  enum
  {
    kNumOutputChannels = 8,
    kNumPeriods = 50
  };

  typedef PcmBuffer<int16_t> BufType;

  AlsaPluginDxO(const std::string& path, uint32_t blockSize, uint32_t periodSize)
      : blockSize_(blockSize),
        periodSize_(periodSize),
        inputs_(3),
        outputs_(5),
        inputOffset_(0),
        outputBuffer_{new int16_t[blockSize_ * kNumOutputChannels]}
  // thread_([this]() { run(); })
  {
    memset(this, 0, sizeof(snd_pcm_ioplug_t));

    auto coeffs = loadFIRCoeffs(path);
    std::cout << "coeffs: " << (coeffs.size()) << std::endl;
    assert(coeffs.size() == 7 && "Coeffs file need to provide 7 FIR transfer functions");

    std::vector<FirMultiChannelCrossover::ConfigType> config{{0, coeffs[0]},
                                                             {0, coeffs[1]},
                                                             // {0, coeffs[2]},
                                                             {1, coeffs[3]},
                                                             {1, coeffs[4]},
                                                             // {1, coeffs[5]},
                                                             {2, coeffs[6]}};

    std::cout << "create Crossover: " << std::endl;

    crossover_ = std::make_unique<FirMultiChannelCrossover>(blockSize_, 3, config, 3);

    for(auto i{0}; i < inputs_.size(); ++i)
    {
      inputs_[i] = crossover_->getInputBuffer(i).data();
    }

    for(auto i{0}; i < outputs_.size(); ++i)
    {
      outputs_[i] = crossover_->getOutputBuffer(i).data();
    }

    std::cout << "Plugin created" << std::endl;
  }

  ~AlsaPluginDxO() {}

  std::vector<std::vector<float>> loadFIRCoeffs(const std::string& path)
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
        if(line.length() > 0 && line[0] == "#")
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

  void print(const snd_pcm_channel_area_t* x, uint32_t offset)
  {
    auto* a = reinterpret_cast<uint16_t*>(snd_pcm_channel_area_addr(x + 0, offset));
    auto* b = reinterpret_cast<uint16_t*>(snd_pcm_channel_area_addr(x + 1, offset));

    std::cout << std::hex << "  a: " << (a) << "\n  b: " << (b) << "\n";
    for(uint32_t i = 0; i < 4; ++i)
    {
      std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0')
                << (a[i * snd_pcm_channel_area_step(x + 0) / sizeof(uint16_t)]) << " ";
      std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0')
                << (b[i * snd_pcm_channel_area_step(x + 1) / sizeof(uint16_t)]) << "\n";
    }
    std::cout << std::dec << std::endl;
  }

  template <bool _HasLfeChannel, typename _InputSampleType>
  uint32_t update(PcmStream<_InputSampleType>& src, uint32_t size)
  {
    auto i{0U};
    while(i < size)
    {
      uint32_t segmentSize = std::min(size - i, blockSize_ - inputOffset_);

      if(_HasLfeChannel)
      {
        src.extractInterleaved(
            segmentSize, inputs_[0] + inputOffset_, inputs_[1] + inputOffset_, inputs_[2] + inputOffset_);
      }
      else
      {
        // std::cout << "  two channels" << std::endl;

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
        // memcpy(outputs_[0], inputs_[0], sizeof(float) * blockSize_);
        // memcpy(outputs_[1], inputs_[1], sizeof(float) * blockSize_);
        //    snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset, 2, blockSize_, 2);
        auto start = std::chrono::high_resolution_clock::now();
        crossover_->updateInputs();
        auto end = std::chrono::high_resolution_clock::now();

        double time_taken = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() * 1e-9;
        totalTime_ += time_taken;
        ++totalBlocks_;
        // std::cout << "time " << time_taken << std::setprecision(9) << " s" << std::endl;

        PcmStream<int16_t> dst(outputBuffer_.get(), kNumOutputChannels);
        dst.loadInterleaved(blockSize_,
                            outputs_[0],
                            outputs_[1],
                            outputs_[2],
                            outputs_[3],
                            outputs_[0],  // unused
                            outputs_[4],
                            outputs_[0],
                            outputs_[0]);

        // if(dstSize++ == 10)
        {
          snd_pcm_sframes_t result = snd_pcm_writei(pcm_, outputBuffer_.get(), blockSize_);
          {
            if(result == -EAGAIN)
            {
              std::cout << "result: " << (result) << std::endl;
              std::this_thread::yield();
            }
            else if(result < 0)
            {
              // snd_output_printf(output_, "underflow\n");  // snd_strerror(result));
              // snd_output_flush(output_);
              snd_pcm_recover(pcm_, result, 0);
              std::cout << "SyncOutputBuffer error: " << (snd_strerror(result)) << std::endl;
            }
          }
        }

        inputOffset_ = 0;
      }
    }
    return 0;
  }

  uint32_t blockSize_{128};
  uint32_t periodSize_;
  std::vector<float*> inputs_{nullptr};
  std::vector<float*> outputs_{nullptr};
  uint32_t inputOffset_{0};
  snd_output_t* output_{nullptr};
  std::unique_ptr<FirMultiChannelCrossover> crossover_;
  snd_pcm_t* pcm_{nullptr};
  snd_pcm_hw_params_t* params_{nullptr};
  std::string pcmName_{};
  std::atomic<uint32_t> streamPos_{0};
  std::unique_ptr<int16_t> outputBuffer_;
  double totalTime_{0};
  uint32_t totalBlocks_{0};
};
