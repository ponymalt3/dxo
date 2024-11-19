#pragma once

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
  RingBuffer(uint32_t numBuffers, uint32_t bufferSize)
      : numBuffers_(numBuffers), bufferSize_(bufferSize), buffer_(new _T[numBuffers_ * bufferSize_])
  {
  }

  _T* getWriteBuffer()
  {
    rptr_.wait(wptr_ - numBuffers_);
    return buffer_.get() + (wptr_ % numBuffers_) * bufferSize_;
  }

  void writeComplete()
  {
    rptr_.wait(wptr_ - numBuffers_);
    ++wptr_;
    wptr_.notify_one();
  }

  _T* getReadBuffer()
  {
    wptr_.wait(rptr_);
    return buffer_.get() + (rptr_ % numBuffers_) * bufferSize_;
  }

  void readComplete()
  {
    wptr_.wait(rptr_);
    ++rptr_;
    rptr_.notify_one();
  }

  void releaseAll()
  {
    wptr_ = rptr_ + 2;
    wptr_.notify_all();
    rptr_.notify_all();
  }

protected:
  uint32_t numBuffers_;
  uint32_t bufferSize_;
  std::unique_ptr<_T> buffer_;
  std::atomic<uint32_t> rptr_{0};
  std::atomic<uint32_t> wptr_{0};
};

class AlsaPluginDxO : public snd_pcm_ioplug_t
{
public:
  enum
  {
    kNumOutputChannels = 8,
    kNumPeriods = 50
  };

  AlsaPluginDxO(const std::string& path, uint32_t blockSize)
      : blockSize_(blockSize),
        inputs_(3),
        outputs_(7),
        inputOffset_(0),
        // outputBuffers_{256, blockSize_ * kNumOutputChannels},
        outputBuffer_{new int16_t[blockSize_ * kNumOutputChannels]},
        outputBuffer2_{new int16_t[blockSize_ * kNumOutputChannels]}
  // writerThread_([this]() { syncOutputBuffer(); })
  {
    memset(this, 0, sizeof(snd_pcm_ioplug_t));

    auto coeffs = loadFIRCoeffs(path);
    std::cout << "coeffs: " << (coeffs.size()) << std::endl;
    assert(coeffs.size() == 7 && "Coeffs file need to provide 7 FIR transfer functions");

    std::vector<FirMultiChannelCrossover::ConfigType> config{{0, coeffs[0]},
                                                             {0, coeffs[1]},
                                                             {0, coeffs[2]},
                                                             {1, coeffs[3]},
                                                             {1, coeffs[4]},
                                                             {1, coeffs[5]},
                                                             {2, coeffs[6]}};

    std::cout << "create Crossover: " << std::endl;

    crossover_ = std::make_unique<FirMultiChannelCrossover>(blockSize_, 3, config, 2);

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

  ~AlsaPluginDxO()
  {
    /* threadRun_ = false;
     outputBuffers_.releaseAll();

     if(writerThread_.joinable())
     {
       std::cout << "Wait for THread" << std::endl;
       writerThread_.join();
       std::cout << "Wait complete" << std::endl;
     }*/
  }

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
        std::vector<float> coeffs;
        std::istringstream iss(line);
        while(!iss.eof())
        {
          double value = 0;
          iss >> value;
          coeffs.push_back(static_cast<float>(value));
        }

        filters.push_back(coeffs);
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
    // std::cout << "Update " << (size) << std::endl;

    PcmStream<int16_t> dst{outputBuffer_.get(), kNumOutputChannels};

    auto i{0U};
    while(i < size)
    {
      uint32_t segmentSize = std::min(size - i, blockSize_ - inputOffset_);
      // std::cout << " Segment " << (segmentSize) << std::endl;

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

      if(inputOffset_ == blockSize_)
      {
        memcpy(outputs_[0], inputs_[0], sizeof(float) * blockSize_);
        memcpy(outputs_[1], inputs_[1], sizeof(float) * blockSize_);
        //  snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset, 2, blockSize_, 2);

        // crossover_->updateInputs();

        dst.loadInterleaved(blockSize_,
                            outputs_[0],
                            outputs_[1],
                            outputs_[2],
                            outputs_[3],
                            outputs_[0],  // unused
                            outputs_[6],
                            outputs_[4],
                            outputs_[5]);

        // syncOutputBuffer();
        // outputBuffers_.writeComplete();
        snd_pcm_sframes_t result = 0;
        uint32_t bufOffset = 0;
        while((result = snd_pcm_writei(pcm_, outputBuffer_.get() + bufOffset, blockSize_ - bufOffset)) !=
              (blockSize_ - bufOffset))
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
            std::cout << "SyncOutputBuffer error: " << (snd_strerror(result)) << std::endl;
            snd_pcm_recover(pcm_, result, 0);
          }
          else
          {
            bufOffset += result;
          }
        }

        inputOffset_ = 0;
        outputBuffer_.swap(outputBuffer2_);
        dst = PcmStream<int16_t>{outputBuffer_.get(), kNumOutputChannels};
      }
    }

    streamPos_ += size;
    return 0;
  }

  void syncOutputBuffer() {}

  uint32_t blockSize_{128};
  std::vector<float*> inputs_{nullptr};
  std::vector<float*> outputs_{nullptr};
  uint32_t inputOffset_{0};
  snd_output_t* output_{nullptr};
  std::unique_ptr<FirMultiChannelCrossover> crossover_;
  snd_pcm_t* pcm_{nullptr};
  snd_pcm_hw_params_t* params_{nullptr};
  std::string pcmName_{};
  uint32_t streamPos_{0};
  // RingBuffer<int16_t> outputBuffers_;
  // std::atomic<bool> threadRun_{true};
  // std::thread writerThread_;
  std::unique_ptr<int16_t> outputBuffer_;
  std::unique_ptr<int16_t> outputBuffer2_;
};
