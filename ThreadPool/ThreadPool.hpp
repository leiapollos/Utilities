#pragma once

#include <array>
#include <vector>
#include <thread>
#include <future>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <type_traits>

class ThreadPool {
public:
    enum class ThreadPriority { LOW, NORMAL, HIGH };

    ThreadPool(const std::vector<ThreadPriority>& priorities = {ThreadPriority::HIGH, ThreadPriority::LOW});
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <
        typename F,
        typename... Args,
        std::enable_if_t<std::is_invocable_v<F&&, Args&&...>, bool> = true,
        typename ReturnType = std::invoke_result_t<F, Args...>
    >
    [[nodiscard]] std::future<ReturnType> enqueue(ThreadPriority priority, F&&, Args&&...);

private:
    void setThreadPriority(std::jthread& thread, ThreadPriority priority);
    
    class _TaskContainerBase {
    public:
        virtual ~_TaskContainerBase() {};
        _TaskContainerBase() = default;
        _TaskContainerBase(const _TaskContainerBase&) = delete;
        _TaskContainerBase& operator=(const _TaskContainerBase&) = delete;

        virtual void operator()() = 0;
    };

    template <typename F>
    class _TaskContainer : public _TaskContainerBase {
    public:
        _TaskContainer(const _TaskContainer&) = delete;
        _TaskContainer& operator=(const _TaskContainer&) = delete;

        _TaskContainer(F&& func) : _f(std::move(func)) {}

        void operator()() override {
            _f();
        }

    private:
        F _f;
    };

    using _taskUPtr = std::unique_ptr<_TaskContainerBase>;
    template <typename F> _TaskContainer(F) -> _TaskContainer<std::decay_t<F>>;

    std::array<std::vector<std::jthread>, 3> _threads;
    std::array<std::deque<_taskUPtr>, 3> _tasks;
    std::mutex _queueMutex;
    std::condition_variable_any _queueCV;
    std::stop_source _stopSource;
};

template <
    typename F,
    typename... Args,
    std::enable_if_t<std::is_invocable_v<F&&, Args&&...>, bool>,
    typename ReturnType
>
std::future<ReturnType> ThreadPool::enqueue(ThreadPriority priority, F&& function, Args&&...args) {
    std::unique_lock<std::mutex> queueLock(_queueMutex, std::defer_lock);

    std::packaged_task<ReturnType()> wrapper([f = std::move(function), largs = std::make_tuple(std::forward<Args>(args)...)]() mutable {
        return std::apply(std::move(f), std::move(largs));
    });
    std::future<ReturnType> future = wrapper.get_future();

    {
        queueLock.lock();
        _tasks[static_cast<int>(priority)].emplace_back(new _TaskContainer(std::move(wrapper)));
        queueLock.unlock();
    }

    _queueCV.notify_one();

    return future;
}
