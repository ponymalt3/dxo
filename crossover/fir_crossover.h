#pragma once

#include <stdint.h>

#include <list>
#include <vector>

#include "../tasks/tasks.h"
#include "convolution.h"

using TaskType = std::shared_ptr<Task>;

class FirMultiChannelCrossover
{
public:
  using ArtifactType = std::vector<fftw_complex>;
  using ConfigType = std::pair<uint32_t, std::vector<float>>;

  FirMultiChannelCrossover(uint32_t blockSize,
                           uint32_t numInputChannels,
                           const std::vector<ConfigType>& channelFilters,
                           uint32_t threads = 3)
      : runner_{threads}
  {
    for(uint32_t i{0}; i < numInputChannels; ++i)
    {
      auto [inputJob, input] = Convolution::getInputTask(blockSize);
      inputJobs_.push_back(inputJob);
      inputBuffer_.push_back(input);
    }

    std::vector<TaskType> finalDeps;
    for(auto& [inputChannel, h] : channelFilters)
    {
      auto conv = std::make_unique<Convolution>(h, blockSize);
      auto [backgroundJobs, output] = conv->getOutputTasks(inputJobs_[inputChannel]);

      outputBuffer_.push_back(output);
      assert(backgroundJobs[1]->isFinal() && "Task must be a final task");
      finalDeps.push_back(backgroundJobs[1]);
      backgroundJobs_.insert(backgroundJobs_.end(), backgroundJobs.begin(), backgroundJobs.end());
      convolutions_.push_back(std::move(conv));
    }

    // combine final jobs into one
    auto combined = Task::create<int>([](Task&) {}, finalDeps);
    backgroundJobs_.push_back(combined);

    runner_.run(backgroundJobs_, false);
  }

  ~FirMultiChannelCrossover() { fftwf_cleanup(); }

  void updateInputs()
  {
    runner_.run(inputJobs_);
    runner_.run(backgroundJobs_, false);
  }

  const RealData& getInputBuffer(uint32_t inputChannel) const
  {
    assert(inputChannel < inputBuffer_.size());
    return inputBuffer_[inputChannel];
  }

  const RealData& getOutputBuffer(uint32_t outputChannel) const
  {
    assert(outputChannel < outputBuffer_.size());
    return outputBuffer_[outputChannel];
  }

protected:
  TaskRunner runner_;
  std::vector<TaskType> inputJobs_;
  std::vector<TaskType> backgroundJobs_;
  std::vector<RealData> inputBuffer_;
  std::vector<RealData> outputBuffer_;
  std::list<std::unique_ptr<Convolution>> convolutions_;
};