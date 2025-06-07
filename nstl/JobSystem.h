#pragma once

#include <thread>
#include <vector>
#include "atomic.h"

namespace nstl {
    class Job;
    class JobCounter;
    class JobQueue;
    class JobSystem;
    class Worker;

    using JobFunction = void(*)(void* data);

    class Job {
    public:
        Job(JobFunction function, JobCounter* counter, void* data);

        void execute();
        bool is_done();

    private:
        void finish();

        JobFunction _function;
        JobCounter* _counter;
        void* _data;
    };

    class JobCounter {
    public:
        JobCounter() : _counter(1) {
        }
        JobCounter(const JobCounter&) = delete;
        JobCounter& operator=(const JobCounter&) = delete;

        void add(size_t count) {
            _counter.fetch_add(count, memory_order::relaxed);
        }

        void finish_one() {
            _counter.fetch_sub(1, memory_order::release);
        }

        bool is_done() const {
            return _counter.load(memory_order::acquire) == 0;
        }

    private:
        friend class JobSystem;
        friend class Worker;

        alignas(hardware_destructive_interference_size) Atomic<ui64> _counter;
    };

    class JobQueue {
    public:
        JobQueue();

        void push(Job* job);
        Job* pop();
        Job* steal();
        size_t size() const;
        void clear();

    private:
        alignas(hardware_destructive_interference_size) Atomic<i64> _bottomIndex;
        alignas(hardware_destructive_interference_size) Atomic<i64> _topIndex;
        std::vector<Job*> _queue;
    };


    class JobSystem {
    public:
        static constexpr size_t MAX_JOBS_PER_THREAD = 65536;
        static_assert(MAX_JOBS_PER_THREAD && (!(MAX_JOBS_PER_THREAD & (MAX_JOBS_PER_THREAD - 1))) && "MAX_JOBS_PER_THREAD must be a power of 2!");

        JobSystem(std::size_t workersCount);
        ~JobSystem();

        [[nodiscard]] Job* create_empty_job() const;
        [[nodiscard]] Job* create_job(const JobFunction function, JobCounter* counter) const;
        [[nodiscard]] Job* create_job(const JobFunction function, JobCounter* counter, void* data) const;

        void run(Job* job);
        void wait(JobCounter* counter);
        void clear_job_queues();
        JobQueue* get_random_job_queue();

    private:
        size_t _workersCount;
        std::vector<Worker*> _workers;
        std::vector<JobQueue*> _queues;
    };

    class Worker {
    public:
        enum class State : unsigned int {
            RUNNING = 0,
            IDLE
        };

        Worker(JobSystem*, JobQueue*);
        Worker(const Worker&) = delete;
        ~Worker();

        void start_background_thread();
        void stop();
        void submit(Job* job);
        void wait(JobCounter* counter);
        bool is_running();

        const std::thread::id& get_thread_id() const;

    private:
        State _state;
        JobQueue* _queue;
        JobSystem* _system;
        std::thread* _thread;
        std::thread::id _threadId;

        Job* get_job();
        void loop();
    };
}
