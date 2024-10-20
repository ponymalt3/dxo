#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <semaphore>
#include <thread>
#include <vector>

#include "thread_safe_list.h"

class Artifact
{
};

template <typename T> class ArtifactData : public Artifact
{
public:
  template <typename... Args> ArtifactData(Args... args) : data_(std::forward<Args>(args)...) {}

  T &getData() { return data_; }

protected:
  T data_;
};

class Task : public std::enable_shared_from_this<Task>, public ThreadSafeList::Node
{
public:
  void execute(const std::function<void(std::shared_ptr<Task>)> &dep_resolved)
  {
    callback_(*this); // Execute the task

    for (auto &d : dependents_)
    {
      if (--(d->dependenciesLeft_) == 0)
      {
        dep_resolved(d);
      }
    }

    // reset dependencies count
    dependenciesLeft_ = dependencies_.size();
  }

  const std::vector<std::shared_ptr<Task>> &getDependencies() const { return dependencies_; }

  template <typename T> T &getArtifact() { return reinterpret_cast<ArtifactData<T> *>(artifact_.get())->getData(); }

  bool isFinal() const { return dependents_.size() == 0; }

  template <typename ArtifactType>
  static std::shared_ptr<Task>
  create(const std::function<void(Task &)> &callback, const std::vector<std::shared_ptr<Task>> &dependencies = {},
         std::unique_ptr<Artifact> artifact = std::make_unique<ArtifactData<ArtifactType>>())
  {
    std::shared_ptr<Task> task{new Task(callback, dependencies, std::move(artifact))};

    for (auto &d : dependencies)
    {
      d->dependents_.push_back(task);
    }

    return task;
  }

  static std::shared_ptr<Task> fromNode(ThreadSafeList::Node *node)
  {
    return static_cast<Task *>(node)->getSharedPointer();
  }

protected:
  Task(std::function<void(Task &)> task, const std::vector<std::shared_ptr<Task>> &dependencies,
       std::unique_ptr<Artifact> artifact)
      : callback_(std::move(task)), dependencies_(dependencies), dependenciesLeft_(dependencies.size()), dependents_{},
        artifact_(std::move(artifact))
  {
  }

  std::shared_ptr<Task> getSharedPointer() { return shared_from_this(); }

  std::function<void(Task &)> callback_;
  std::vector<std::shared_ptr<Task>> dependencies_;
  std::atomic<uint32_t> dependenciesLeft_{0};     // Number of dependencies yet to complete
  std::vector<std::shared_ptr<Task>> dependents_; // Tasks that depend on this one
  std::unique_ptr<Artifact> artifact_{nullptr};
};

class TaskRunner
{
public:
  explicit TaskRunner(uint32_t numThreads)
  {
    for (uint32_t i = 0; i < numThreads; ++i)
    {
      workers_.emplace_back([this] { threadRun(false); });
    }
  }

  ~TaskRunner()
  {
    stop_.store(true); // Signal threads to stop
    {
      std::unique_lock lock(mutex_);
      state_ = 0xFFFFFFFF;
    }
    cv_.notify_all();

    for (auto &worker : workers_)
    {
      if (worker.joinable())
      {
        worker.join();
      }
    }
  }

  bool run(const std::vector<std::shared_ptr<Task>> &tasks)
  {
    std::shared_ptr<Task> finalTask{nullptr};

    for (auto &task : tasks)
    {
      if (task->isFinal())
      {
        if (finalTask)
        {
          std::cout << "errr" << std::endl;
          return false;
        }

        finalTask = task;
      }

      if (task->getDependencies().size() == 0)
      {
        activeTasks_.push(task.get());
      }
    }

    incrementState();
    threadRun(true);

    finalTaskReady_.acquire();
    finalTask->execute([](std::shared_ptr<Task>) {});

    return true;
  }

protected:
  void incrementState(uint32_t increment = 1)
  {
    {
      std::unique_lock lock(mutex_);
      state_ += increment;
    }
    cv_.notify_all();
  }

  void waitFor(uint32_t state)
  {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this, state] { return state_ >= state; });
  }

  void threadRun(bool master)
  {
    uint64_t state{1};

    waitFor(state);
    while (!stop_.load())
    {
      auto task = activeTasks_.pop();

      if (task == nullptr)
      {
        if (master)
        {
          return;
        }

        std::cout << "Sleeping..." << std::endl;
        waitFor(++state);
      }
      else
      {
        bool restartWorker = false;
        Task::fromNode(task)->execute(
            [this, &restartWorker](std::shared_ptr<Task> task)
            {
              if (!task->isFinal())
              {
                if (activeTasks_.push(task.get()))
                {
                  restartWorker = true;
                }
              }
              else
              {
                finalTaskReady_.release();
              }
            });

        if (restartWorker)
        {
          // restart worker
          incrementState();
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
};