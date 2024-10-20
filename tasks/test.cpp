#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "tasks.h"

class TaskTest : public testing::Test {
   public:
    using ArtifactType = std::array<float, 256>;

    static std::mutex &GetMutex() {
        static std::mutex m;
        return m;
    }

    static void copy(ArtifactType &src, ArtifactType &result,
                     uint32_t sleepMs = 0) {
        result = src;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        {
            // std::lock_guard lock(GetMutex());
            // std::cout<<"copy complete\n";
        }
    }

    static void sum(Task &task, ArtifactType &result, uint32_t sleepMs = 0) {
        for (auto &i : result) {
            i = 0.0f;
        }

        for (auto &dep : task.getDependencies()) {
            for (uint32_t i{0}; i < 256; ++i) {
                result[i] += dep->getArtifact<ArtifactType>()[i];
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        {
            // std::lock_guard lock(GetMutex());
            // std::cout<<"sum complete "<<(result[0])<<"\n";
        }
    }

    TaskRunner runner_{3};
    std::vector<std::shared_ptr<Task>> tasks_;
};

TEST_F(TaskTest, Test_TasksumningOrder) {
    std::array<ArtifactType, 30> inputs;

    for (uint32_t i{0}; i < 30; ++i) {
        inputs[i].fill(i + 1);
        tasks_.push_back(Task::create<ArtifactType>([&inputs, i](Task &task) {
            copy(inputs[i], task.getArtifact<ArtifactType>(), 10);
        }));
    }

    // next
    tasks_.push_back(Task::create<ArtifactType>(
        [](Task &task) {
            sum(task, task.getArtifact<ArtifactType>(), 5 + std::rand() % 5);
        },
        std::vector<std::shared_ptr<Task>>(tasks_.begin() + 0,
                                           tasks_.begin() + 10)));

    tasks_.push_back(Task::create<ArtifactType>(
        [](Task &task) {
            sum(task, task.getArtifact<ArtifactType>(), 5 + std::rand() % 5);
        },
        std::vector<std::shared_ptr<Task>>(tasks_.begin() + 10,
                                           tasks_.begin() + 20)));

    tasks_.push_back(Task::create<ArtifactType>(
        [](Task &task) {
            sum(task, task.getArtifact<ArtifactType>(), 5 + std::rand() % 5);
        },
        std::vector<std::shared_ptr<Task>>(tasks_.begin() + 20,
                                           tasks_.begin() + 30)));

    // next
    tasks_.push_back(Task::create<ArtifactType>(
        [](Task &task) {
            sum(task, task.getArtifact<ArtifactType>(), 5 + std::rand() % 10);
        },
        {tasks_[30], tasks_[31]}));

    tasks_.push_back(Task::create<ArtifactType>(
        [](Task &task) {
            sum(task, task.getArtifact<ArtifactType>(), 5 + std::rand() % 10);
        },
        {tasks_[31], tasks_[32]}));

    // next
    tasks_.push_back(Task::create<ArtifactType>(
        [](Task &task) {
            sum(task, task.getArtifact<ArtifactType>(), std::rand() % 101);
        },
        {tasks_[33], tasks_[34], tasks_[29]}));

    runner_.run(tasks_);

    EXPECT_EQ(tasks_[35]->getArtifact<ArtifactType>()[0], 650.0f);
}