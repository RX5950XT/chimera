#include "ThreadPool.h"

namespace chimera::utils {

ThreadPool::ThreadPool(size_t numThreads) {
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_condition.wait(lock, [this] { return m_stop || !m_tasks.empty(); });
                    if (m_stop && m_tasks.empty()) return;
                    task = std::move(m_tasks.front());
                    m_tasks.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_condition.notify_all();
    for (auto &worker : m_workers) {
        if (worker.joinable()) worker.join();
    }
}

} // namespace chimera::utils
