#pragma once
#include <atomic>
#include <memory>

class ThreadSafeList
{
public:
  struct Node
  {
    Node* next_;
  };

  Node* pop()
  {
    Node* currentHead{nullptr};

    do
    {
      currentHead = head_.load(std::memory_order_relaxed);

      if(currentHead == nullptr)
      {
        return nullptr;
      }
    } while(!head_.compare_exchange_weak(
        currentHead, currentHead->next_, std::memory_order_release, std::memory_order_relaxed));

    return currentHead;
  }

  bool push(Node* node)
  {
    // https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange

    Node* currentHead{nullptr};

    do
    {
      currentHead = node->next_ = head_.load(std::memory_order_relaxed);
    } while(!head_.compare_exchange_weak(
        currentHead, node, std::memory_order_release, std::memory_order_relaxed));

    return currentHead == nullptr;
  }

protected:
  std::atomic<Node*> head_{nullptr};
};