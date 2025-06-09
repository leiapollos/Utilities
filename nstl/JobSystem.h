#pragma once

#include "atomic.h"
#include "thread.h"
#include "vector.h"

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

        void add(u64 count) {
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

        alignas(hardware_destructive_interference_size) Atomic<u64> _counter;
    };

    class JobSystem {
    public:
        static constexpr u64 MAX_JOBS_PER_THREAD = 65536;
        static_assert(MAX_JOBS_PER_THREAD && (!(MAX_JOBS_PER_THREAD & (MAX_JOBS_PER_THREAD - 1))) && "MAX_JOBS_PER_THREAD must be a power of 2!");

        JobSystem(u64 workersCount);
        ~JobSystem();

        [[nodiscard]] static Job* create_empty_job();
        [[nodiscard]] static Job* create_job(const JobFunction function, JobCounter* counter);
        [[nodiscard]] static Job* create_job(const JobFunction function, JobCounter* counter, void* data) ;

        void run(Job* job);
        void wait(JobCounter* counter);
        void clear_job_queues();
        JobQueue* get_random_job_queue();

    private:
        u64 _workersCount;
        Vector<Worker*> _workers;
        Vector<JobQueue*> _queues;
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
        u32 xor_shift_rand();

        const Thread::thread_id& get_thread_id() const;

    private:
        State _state;
        JobQueue* _queue;
        JobSystem* _system;
        Thread* _thread;
        Thread::thread_id _threadId;
        u32 _randomSeed;

        Job* get_job();
        void loop();
    };

    class JobQueue {
    public:
        JobQueue();

        void push(Job* job);
        Job* pop();
        Job* steal();
        u32 size() const;
        void clear();

    private:
        alignas(hardware_destructive_interference_size) Atomic<i64> _bottomIndex;
        alignas(hardware_destructive_interference_size) Atomic<i64> _topIndex;
        Vector<Job*, JobSystem::MAX_JOBS_PER_THREAD * sizeof(Job)> _queue;
    };
}
