#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <semaphore>
#include <thread>
#include <vector>

#include "thread_safe_list.h"

class Artifact
{
public:
  virtual ~Artifact() {}
};

template <typename T>
class ArtifactImpl : public Artifact
{
public:
  ArtifactImpl(T&& data) : data_(std::move(data)) {}
  T& getData() { return data_; }

protected:
  T data_;
};

class Task : public std::enable_shared_from_this<Task>, public ThreadSafeList::Node
{
public:
  void execute(const std::function<void(std::shared_ptr<Task>)>& dep_resolved)
  {
    callback_(*this);  // Execute the task

    for(auto& d : dependents_)
    {
      if(--(d->dependenciesLeft_) == 0)
      {
        dep_resolved(d);
      }
    }

    // reset dependencies count
    dependenciesLeft_ = dependencies_.size();
  }

  const std::vector<std::shared_ptr<Task>>& getDependencies() const { return dependencies_; }

  template <typename T>
  T& getArtifact()
  {
    return static_cast<ArtifactImpl<T>*>(artifact_.get())->getData();
  }

  void reset()
  {
    if(reset_)
    {
      reset_();
    }
  }

  bool isFinal() const { return dependents_.size() == 0; }

  template <typename ArtifactType>
  static std::shared_ptr<Task> create(const std::function<void(Task&)>& callback,
                                      const std::vector<std::shared_ptr<Task>>& dependencies = {},
                                      ArtifactType&& artifact = ArtifactType(),
                                      const std::function<void()> reset = {})
  {
    std::shared_ptr<Task> task{new Task(
        callback, dependencies, std::make_unique<ArtifactImpl<ArtifactType>>(std::move(artifact)), reset)};

    for(auto& d : dependencies)
    {
      d->dependents_.push_back(task);
    }

    return task;
  }

  static std::shared_ptr<Task> fromNode(ThreadSafeList::Node* node)
  {
    return static_cast<Task*>(node)->getSharedPointer();
  }

protected:
  Task(std::function<void(Task&)> task,
       const std::vector<std::shared_ptr<Task>>& dependencies,
       std::unique_ptr<Artifact> artifact,
       const std::function<void()>& reset)
      : callback_(std::move(task)),
        dependencies_(dependencies),
        dependenciesLeft_(dependencies.size()),
        dependents_{},
        artifact_(std::move(artifact)),
        reset_(reset)
  {
  }

  std::shared_ptr<Task> getSharedPointer() { return shared_from_this(); }

  std::function<void(Task&)> callback_;
  std::vector<std::shared_ptr<Task>> dependencies_;
  std::atomic<uint32_t> dependenciesLeft_{0};      // Number of dependencies yet to complete
  std::vector<std::shared_ptr<Task>> dependents_;  // Tasks that depend on this one
  std::unique_ptr<Artifact> artifact_{nullptr};
  std::function<void()> reset_{};
};

class TaskRunner
{
public:
  explicit TaskRunner(uint32_t numThreads)
  {
    for(uint32_t i = 0; i < numThreads; ++i)
    {
      workers_.emplace_back([this] { threadRun(); });
    }
  }

  ~TaskRunner()
  {
    stop_.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cv_.notify_all();

    for(auto& worker : workers_)
    {
      if(worker.joinable())
      {
        worker.join();
      }
    }
  }

  void run(const std::vector<std::shared_ptr<Task>>& tasks, bool wait = true)
  {
    std::shared_ptr<Task> finalTask{nullptr};

    for(auto& task : tasks)
    {
      if(task->isFinal())
      {
        finalTask_ = task;
      }

      if(task->getDependencies().size() == 0)
      {
        activeTasks_.push(task.get());
      }
    }

    restartWorker();

    if(finalTask_ && wait)
    {
      // threadRun(true);
      finalTaskReady_.acquire();
      finalTask_->execute(nullptr);
      finalTask_ = nullptr;
    }
  }

protected:
  void restartWorker()
  {
    ++epoch_;
    cv_.notify_all();
  }

  void threadRun()
  {
    while(!stop_.load())
    {
      const auto epoch = epoch_.load();
      auto task = activeTasks_.pop();

      if(task == nullptr)
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this, epoch]() { return epoch != epoch_ || stop_; });
      }
      else
      {
        auto listWasEmpty = false;
        Task::fromNode(task)->execute([this, &listWasEmpty](std::shared_ptr<Task> task) {
          if(!task->isFinal())
          {
            if(activeTasks_.push(task.get()))
            {
              listWasEmpty = true;
            }
          }
          else
          {
            finalTaskReady_.release();
          }
        });

        if(listWasEmpty)
        {
          restartWorker();
        }
      }
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  ThreadSafeList activeTasks_;
  std::vector<std::thread> workers_;
  std::atomic<bool> stop_{false};
  std::binary_semaphore finalTaskReady_{0};
  std::shared_ptr<Task> finalTask_{nullptr};
  std::atomic<uint64_t> epoch_{0};
};
