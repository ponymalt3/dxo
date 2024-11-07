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

  bool isFinal() const { return dependents_.size() == 0; }

  template <typename ArtifactType>
  static std::shared_ptr<Task> create(const std::function<void(Task&)>& callback,
                                      const std::vector<std::shared_ptr<Task>>& dependencies = {},
                                      ArtifactType&& artifact = ArtifactType())
  {
    std::shared_ptr<Task> task{
        new Task(callback, dependencies, std::make_unique<ArtifactImpl<ArtifactType>>(std::move(artifact)))};

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
       std::unique_ptr<Artifact> artifact)
      : callback_(std::move(task)),
        dependencies_(dependencies),
        dependenciesLeft_(dependencies.size()),
        dependents_{},
        artifact_(std::move(artifact))
  {
  }

  std::shared_ptr<Task> getSharedPointer() { return shared_from_this(); }

  std::function<void(Task&)> callback_;
  std::vector<std::shared_ptr<Task>> dependencies_;
  std::atomic<uint32_t> dependenciesLeft_{0};      // Number of dependencies yet to complete
  std::vector<std::shared_ptr<Task>> dependents_;  // Tasks that depend on this one
  std::unique_ptr<Artifact> artifact_{nullptr};
};

class TaskRunner
{
public:
  explicit TaskRunner(uint32_t numThreads)
  {
    for(uint32_t i = 0; i < numThreads; ++i)
    {
      workers_.emplace_back([this] { threadRun(false); });
    }
  }

  ~TaskRunner()
  {
    stop_.store(true);  // Signal threads to stop
    {
      std::unique_lock lock(mutex_);
      state_ = 0xFFFFFFFF;
    }
    cv_.notify_all();

    for(auto& worker : workers_)
    {
      if(worker.joinable())
      {
        worker.join();
      }
    }
  }

  bool run(const std::vector<std::shared_ptr<Task>>& tasks, bool wait = true)
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
      threadRun(true);
      finalTaskReady_.acquire();
      finalTask_->execute(nullptr);
      finalTask_ = nullptr;
    }

    return true;
  }

protected:
  void restartWorker(bool all = true)
  {
    if(all)
    {
      cv_.notify_all();
    }
    else
    {
      cv_.notify_one();
    }
  }

  void wait()
  {
    std::unique_lock lock(mutex_);
    cv_.wait(lock);
  }

  void threadRun(bool master)
  {
    uint64_t state{1};

    wait();
    while(!stop_.load())
    {
      auto task = activeTasks_.pop();

      if(task == nullptr)
      {
        if(master)
        {
          return;
        }

        wait();
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
          // restart worker
          restartWorker();
        }
      }
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  uint64_t state_{0U};
  ThreadSafeList activeTasks_;
  std::vector<std::thread> workers_;
  std::atomic<bool> stop_{false};
  std::binary_semaphore finalTaskReady_{0};
  std::shared_ptr<Task> finalTask_{nullptr};
};