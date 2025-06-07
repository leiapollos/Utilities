//
// Created by André Leite on 07/06/2025.
//

#include "JobSystem.h"

#include <cassert>
#include <random>

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
        _bottomIndex = 0;
        _topIndex = 0;
        _queue.resize(JobSystem::MAX_JOBS_PER_THREAD);
    }

    void JobQueue::push(Job* job) {
        int bottom = _bottomIndex.load(std::memory_order_acquire);
        _queue[bottom & (JobSystem::MAX_JOBS_PER_THREAD - 1)] = job;
        _bottomIndex.store(bottom + 1, std::memory_order_release);
    }

    Job* JobQueue::pop() {
        int bottom = _bottomIndex.load(std::memory_order_relaxed) - 1;
        _bottomIndex.store(bottom, std::memory_order_relaxed);
        int top = _topIndex.load(std::memory_order_acquire);

        if (top <= bottom) {
            Job* job = _queue[bottom & (JobSystem::MAX_JOBS_PER_THREAD - 1)];
            if (top == bottom) {
                if (_topIndex.compare_exchange_strong(top, top + 1,
                                                      std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    _bottomIndex.store(bottom + 1, std::memory_order_relaxed);
                    return job;
                                                      } else {
                                                          _bottomIndex.store(bottom + 1, std::memory_order_relaxed);
                                                          return nullptr;
                                                      }
            }

            return job;
        } else {
            _bottomIndex.store(bottom + 1, std::memory_order_relaxed);
            return nullptr;
        }
    }

    Job* JobQueue::steal() {
        int top = _topIndex.load(std::memory_order_acquire);
        int bottom = _bottomIndex.load(std::memory_order_acquire);
        if (top < bottom) {
            Job* job = _queue[top & (JobSystem::MAX_JOBS_PER_THREAD - 1)];

            int expectedTop = top;
            if (_topIndex.compare_exchange_strong(expectedTop, top + 1,
                                                  std::memory_order_seq_cst, std::memory_order_relaxed)) {
                return job;
                                                  }
            return nullptr;
        }

        return nullptr;
    }

    size_t JobQueue::size() const {
        return _bottomIndex.load(std::memory_order_relaxed) -
               _topIndex.load(std::memory_order_relaxed);
    }

    void JobQueue::clear() {
        _bottomIndex.store(0, std::memory_order_relaxed);
        _topIndex.store(0, std::memory_order_relaxed);
    }


    JobSystem::JobSystem(size_t workersCount) : _workersCount(workersCount) {
        _queues.reserve(workersCount);
        _workers.reserve(workersCount);

        JobQueue* queue = new JobQueue();
        _queues.push_back(queue);
        Worker* mainThreadWorker = new Worker(this, queue);
        _workers.push_back(mainThreadWorker);
        g_thisThreadWorker = mainThreadWorker;

        for (size_t i = 0; i < workersCount; ++i) {
            JobQueue* queue = new JobQueue();
            _queues.push_back(queue);
            Worker* worker = new Worker(this, queue);
            _workers.push_back(worker);
        }

        for (size_t i = 1; i <= workersCount; ++i) {
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

    Job* JobSystem::create_empty_job() const {
        return create_job(nullptr, nullptr);
    }

    Job* JobSystem::create_job(const JobFunction function, JobCounter* counter) const {
        return create_job(function, counter, nullptr);
    }

    Job* JobSystem::create_job(const JobFunction function, JobCounter* counter, void* data) const {
        struct AlignedJobStorage {
            alignas(alignof(Job)) std::byte storage[sizeof(Job)];
        };
        static thread_local AlignedJobStorage jobPoolMemory[MAX_JOBS_PER_THREAD];
        Job* jobPool = reinterpret_cast<Job*>(jobPoolMemory);

        static thread_local size_t allocatedJobs = 0;
        const size_t jobIndex = allocatedJobs++;

        Job* job = &jobPool[jobIndex & (MAX_JOBS_PER_THREAD - 1)];
        assert(job->is_done() && "Job memory is being overwritten while still in use!");
        new(job) Job(function, counter, data);

        return job;
    }

    void JobSystem::run(Job* job) {
        assert(g_thisThreadWorker != nullptr && "JobSystem::Run called from a non-worker thread");
        g_thisThreadWorker->submit(job);
    }

    void JobSystem::wait(JobCounter* counter) {
        assert(g_thisThreadWorker != nullptr && "JobSystem::Wait called from a non-worker thread");
        g_thisThreadWorker->wait(counter);
    }

    JobQueue* JobSystem::get_random_job_queue() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> distribution(0, _workersCount);

        size_t index = static_cast<size_t>(std::round(distribution(gen)));
        return _queues[index];
    }

    void JobSystem::clear_job_queues() {
        for (JobQueue* queue: _queues) {
            queue->clear();
        }
    }

    Worker::Worker(JobSystem* system, JobQueue* queue)
        : _system(system), _queue(queue), _thread(nullptr), _threadId(std::this_thread::get_id()) {
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
        _thread = new std::thread(&Worker::loop, this);
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
                std::this_thread::yield();
                return nullptr;
            }

            if (_queue == randomQueue) {
                std::this_thread::yield();
                return nullptr;
            }

            job = randomQueue->steal();
            if (job == nullptr) {
                std::this_thread::yield();
                return nullptr;
            }
        }
        return job;
    }

    bool Worker::is_running() {
        return (_state == State::RUNNING);
    }

    const std::thread::id& Worker::get_thread_id() const {
        return _threadId;
    }
}