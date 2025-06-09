//
// Created by André Leite on 07/06/2025.
//

#include "JobSystem.h"

#include "os/core/assert.h"

#include <new> // Cries :'( Still haven't found a good solution for avoiding this

namespace nstl {
    thread_local Worker* g_thisThreadWorker = nullptr;

    Job::Job(JobFunction function, JobCounter* counter, void* data)
        : _function(function), _counter(counter), _data(data) {
    }

    void Job::execute() {
        if (_function != nullptr) {
            _function(_data);
        }
        finish();
    }

    bool Job::is_done() {
        if (_counter != nullptr) [[likely]] {
            bool done = _counter->is_done();
            return done;
        }
        return true;
    }

    void Job::finish() {
        if (_counter) {
            _counter->finish_one();
        }
    }

    JobQueue::JobQueue() {
        _bottomIndex.store(0);
        _topIndex.store(0);
        _queue.reserve(JobSystem::MAX_JOBS_PER_THREAD);
    }

    void JobQueue::push(Job* job) {
        i64 bottom = _bottomIndex.load(memory_order::acquire);
        _queue[bottom & (JobSystem::MAX_JOBS_PER_THREAD - 1)] = job;
        _bottomIndex.store(bottom + 1, memory_order::release);
    }

    Job* JobQueue::pop() {
        i64 bottom = _bottomIndex.load(memory_order::relaxed) - 1;
        _bottomIndex.store(bottom, memory_order::relaxed);
        i64 top = _topIndex.load(memory_order::acquire);

        if (top <= bottom) {
            Job* job = _queue[bottom & (JobSystem::MAX_JOBS_PER_THREAD - 1)];
            if (top == bottom) {
                if (_topIndex.compare_exchange_strong(top, top + 1,
                                                      memory_order::seq_cst, memory_order::relaxed)) {
                    _bottomIndex.store(bottom + 1, memory_order::relaxed);
                    return job;
                } else {
                    _bottomIndex.store(bottom + 1, memory_order::relaxed);
                    return nullptr;
                }
            }

            return job;
        } else {
            _bottomIndex.store(bottom + 1, memory_order::relaxed);
            return nullptr;
        }
    }

    Job* JobQueue::steal() {
        i64 top = _topIndex.load(memory_order::acquire);
        i64 bottom = _bottomIndex.load(memory_order::acquire);
        if (top < bottom) {
            Job* job = _queue[top & (JobSystem::MAX_JOBS_PER_THREAD - 1)];

            if (_topIndex.compare_exchange_strong(top, top + 1,
                                                  memory_order::seq_cst, memory_order::relaxed)) {
                return job;
            }
            return nullptr;
        }

        return nullptr;
    }

    u32 JobQueue::size() const {
        return _bottomIndex.load(memory_order::relaxed) -
               _topIndex.load(memory_order::relaxed);
    }

    void JobQueue::clear() {
        _bottomIndex.store(0, memory_order::relaxed);
        _topIndex.store(0, memory_order::relaxed);
    }


    JobSystem::JobSystem(u64 workersCount) : _workersCount(workersCount) {
        _queues.reserve(workersCount);
        _workers.reserve(workersCount);

        JobQueue* mainThreadQueue = new JobQueue();
        _queues.push_back(mainThreadQueue);
        Worker* mainThreadWorker = new Worker(this, mainThreadQueue);
        _workers.push_back(mainThreadWorker);
        g_thisThreadWorker = mainThreadWorker;

        for (u64 i = 0; i < workersCount; ++i) {
            JobQueue* queue = new JobQueue();
            _queues.push_back(queue);
            Worker* worker = new Worker(this, queue);
            _workers.push_back(worker);
        }

        for (u64 i = 1; i <= workersCount; ++i) {
            _workers[i]->start_background_thread();
        }
    }

    JobSystem::~JobSystem() {
        for (Worker* worker: _workers) {
            delete worker;
        }
        _workers.clear();

        for (JobQueue* queue: _queues) {
            delete queue;
        }
        _queues.clear();
    }

    Job* JobSystem::create_empty_job() {
        return create_job(nullptr, nullptr);
    }

    Job* JobSystem::create_job(const JobFunction function, JobCounter* counter) {
        return create_job(function, counter, nullptr);
    }

    Job* JobSystem::create_job(const JobFunction function, JobCounter* counter, void* data) {
        struct AlignedJobStorage {
            alignas(alignof(Job)) u8 storage[sizeof(Job)];
        };
        static thread_local AlignedJobStorage jobPoolMemory[MAX_JOBS_PER_THREAD];
        Job* jobPool = reinterpret_cast<Job*>(jobPoolMemory);

        static thread_local u64 allocatedJobs = 0;
        const u64 jobIndex = allocatedJobs++;

        Job* job = &jobPool[jobIndex & (MAX_JOBS_PER_THREAD - 1)];
        NSTL_ASSERT(job->is_done() && "Job memory is being overwritten while still in use!");
        new(job) Job(function, counter, data);

        return job;
    }

    void JobSystem::run(Job* job) {
        NSTL_ASSERT(g_thisThreadWorker != nullptr && "JobSystem::run called from a non-worker thread");
        g_thisThreadWorker->submit(job);
    }

    void JobSystem::wait(JobCounter* counter) {
        NSTL_ASSERT(g_thisThreadWorker != nullptr && "JobSystem::wait called from a non-worker thread");
        g_thisThreadWorker->wait(counter);
    }

    JobQueue* JobSystem::get_random_job_queue() {
        NSTL_ASSERT(g_thisThreadWorker != nullptr &&
            "JobSystem::get_random_job_queue must be called from a worker thread");
        u32 randomValue = g_thisThreadWorker->xor_shift_rand();
        const u64 range = _workersCount + 1;
        const u64 index = (static_cast<u64>(randomValue) * range) >> 32;
        return _queues[index];
    }

    void JobSystem::clear_job_queues() {
        for (JobQueue* queue: _queues) {
            queue->clear();
        }
    }

    Worker::Worker(JobSystem* system, JobQueue* queue)
        : _system(system), _queue(queue), _thread(nullptr), _threadId(Thread::get_current_thread_id()) {
        _randomSeed = static_cast<u32>(reinterpret_cast<uintptr>(this));
        if (_randomSeed == 0) { // Should never happen, but doesn't hurt
            _randomSeed = 0xBAD5EEDBAD5EEDULL;;
        }
    }

    Worker::~Worker() {
        stop();
        if (_thread != nullptr) {
            _thread->join();
            delete _thread;
        }
    }

    void Worker::start_background_thread() {
        _state = State::RUNNING;
        _thread = new Thread();
        _thread->start<Worker>(this, &Worker::loop);
        _threadId = _thread->get_id();
    }

    void Worker::stop() {
        _state = State::IDLE;
    }

    void Worker::submit(Job* job) {
        _queue->push(job);
    }

    void Worker::wait(JobCounter* counter) {
        while (!counter->is_done()) {
            Job* job = get_job();
            if (job != nullptr) {
                job->execute();
                // No need to delete, since we will just override things when we fetch the same memory again from the ring buffer.
            }
        }
    }

    void Worker::loop() {
        g_thisThreadWorker = this;

        while (is_running()) {
            Job* job = get_job();
            if (job != nullptr) {
                job->execute();
                // No need to delete, since we will just override things when we fetch the same memory again from the ring buffer.
            }
        }
    }

    Job* Worker::get_job() {
        Job* job = _queue->pop();

        if (job == nullptr) {
            JobQueue* randomQueue = _system->get_random_job_queue();
            if (randomQueue == nullptr) {
                Thread::yield();
                return nullptr;
            }

            if (_queue == randomQueue) {
                Thread::yield();
                return nullptr;
            }

            job = randomQueue->steal();
            if (job == nullptr) {
                Thread::yield();
                return nullptr;
            }
        }
        return job;
    }

    bool Worker::is_running() {
        return (_state == State::RUNNING);
    }

    const Thread::thread_id& Worker::get_thread_id() const {
        return _threadId;
    }

    u32 Worker::xor_shift_rand() {
        u32 x = _randomSeed;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        _randomSeed = x;
        return x;
    }
}
