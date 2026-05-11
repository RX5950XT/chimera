#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

namespace chimera::utils {

/**
 * @brief Simple thread pool for offloading work from the UI thread.
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    template<typename F, typename... Args>
    auto enqueue(F &&f, Args &&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_stop) throw std::runtime_error("enqueue on stopped ThreadPool");
            m_tasks.emplace([task]() { (*task)(); });
        }
        m_condition.notify_one();
        return res;
    }

private:
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_stop = false;
};

} // namespace chimera::utils
