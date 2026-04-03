#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class PersistExecutor
{
public:
    PersistExecutor() = default;
    ~PersistExecutor() { stop(); }

    void start(int workerCount)
    {
        stop();
        stop_ = false;
        const int count = workerCount > 0 ? workerCount : 1;
        workers_.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            workers_.emplace_back([this]() { workerLoop(); });
    }

    void stop()
    {
        stop_ = true;
        cv_.notify_all();
        for (auto &worker : workers_) {
            if (worker.joinable())
                worker.join();
        }
        workers_.clear();

        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    void enqueue(std::function<void()> task)
    {
        if (!task)
            return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(task));
        }
        cv_.notify_one();
    }

private:
    void workerLoop()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
                if (queue_.empty()) {
                    if (stop_)
                        return;
                    continue;
                }
                task = std::move(queue_.front());
                queue_.pop_front();
            }

            if (task)
                task();
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    std::atomic_bool stop_ = false;
};
