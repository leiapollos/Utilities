#include "ThreadPool.hpp"

ThreadPool::ThreadPool(size_t threadCount) {
    _threads.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i) {
        _threads.emplace_back(std::thread([&]() {
            std::unique_lock<std::mutex> queueLock(_queueMutex, std::defer_lock);
            while (true) {
                queueLock.lock();
                _queueCV.wait(queueLock,
                    [&]() -> bool { return !_tasks.empty() || _stopping; });

                if (_stopping && _tasks.empty()) return;

                auto tempTask = std::move(_tasks.front());
                _tasks.pop_front();

                queueLock.unlock();

                (*tempTask)();
            }
        }));
    }
}

ThreadPool::~ThreadPool() {
    _stopping = true;
    _queueCV.notify_all();

    for (std::thread& thread : _threads) {
        thread.join();
    }
}