#include "ThreadPool.hpp"

ThreadPool::ThreadPool(const std::vector<ThreadPriority>& priorities) {
    for (const auto& priority : priorities) {
            int priorityIndex = static_cast<int>(priority);
            _threads[priorityIndex].emplace_back(std::thread([&, priorityIndex]() {
                std::unique_lock<std::mutex> queueLock(_queueMutex, std::defer_lock);
                
                while (true) {
                    queueLock.lock();
                    _queueCV.wait(queueLock,
                        [&]() -> bool {
                        return !_tasks[priorityIndex].empty() || _stopping;
                        
                    });

                    if (_stopping && _tasks[priorityIndex].empty()) return;

                    auto tempTask = std::move(_tasks[priorityIndex].front());
                    _tasks[priorityIndex].pop_front();

                    queueLock.unlock();

                    (*tempTask)();
                }
            }));
        
            setThreadPriority(_threads[priorityIndex].back(), priority);
        }
}

ThreadPool::~ThreadPool() {
    _stopping = true;
    _queueCV.notify_all();

    for (auto& priorityThreads : _threads) {
        for (std::thread& thread : priorityThreads) {
            thread.join();
        }
    }
}

void ThreadPool::setThreadPriority(std::thread& thread, ThreadPriority priority) {
    #ifdef _WIN32
    // Set priority for Windows
    HANDLE currentThread = thread.native_handle();
    int winPriority;
    switch (priority) {
        case ThreadPriority::LOW: {
            winPriority = THREAD_PRIORITY_LOWEST;
            break;
        }
        case ThreadPriority::HIGH: {
            winPriority = THREAD_PRIORITY_HIGHEST;
            break;
        }
        case ThreadPriority::NORMAL:
        default: {
            winPriority = THREAD_PRIORITY_NORMAL;
            break;
        }
    }
    SetThreadPriority(currentThread, winPriority);
    #else
    // Set priority for UNIX systems using pthread
    pthread_t currentThread = thread.native_handle();
    int policy;
    sched_param param;
    pthread_getschedparam(currentThread, &policy, &param);
    switch (priority) {
        case ThreadPriority::LOW: {
            param.sched_priority = sched_get_priority_min(policy);
            break;
        }
        case ThreadPriority::HIGH: {
            param.sched_priority = sched_get_priority_max(policy);
            break;
        }
        default:
            break;
    }
    pthread_setschedparam(currentThread, policy, &param);
    #endif
}
