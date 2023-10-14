#pragma once
#include <thread>

#include "Job.hpp"
#include "JobSystemTypes.hpp"

namespace jobSystem
{
	class BaseCounter;
	class Counter;
	class Fiber;
	class Thread;
	struct ThreadLocalStorage;

	struct ManagerOptions
	{
		ManagerOptions() : numThreads(std::thread::hardware_concurrency()) {}
		~ManagerOptions() = default;

		uint32_t numThreads;
		uint32_t numFibers = std::thread::hardware_concurrency() * 2;

		uint64_t highPriorityQueueSize = 512ULL;
		uint64_t normalPriorityQueueSize = 512ULL * 4ULL;
		uint64_t lowPriorityQueueSize = 512ULL * 8ULL;

		bool shutdownAfterMainCallback = true;
	};

	class Manager
	{
		friend class BaseCounter;

	public:

		using MainType = void(*)(Manager*);

		Manager(const ManagerOptions & = ManagerOptions());
		Manager(MainType main, const ManagerOptions & = ManagerOptions());
		~Manager();

		void run(MainType main);
		void shutdown(bool blocking);
		void scheduleJob(JobPriority priority, const JobInfo& info);
		void waitForCounter(BaseCounter* counter, uint32_t = 0);
		void waitForSingle(JobPriority priority, JobInfo info);

		inline bool isShuttingDown() const
		{
			return _shuttingDown.load(std::memory_order_acquire);
		}

		template <typename Callable, typename... Args>
		inline void scheduleJob(JobPriority priority, Callable callable, Args... args)
		{
			scheduleJob(priority, JobInfo(callable, args...));
		}

		template <typename Callable, typename... Args>
		inline void scheduleJob(JobPriority priority, BaseCounter* ctr, Callable callable, Args... args)
		{
			scheduleJob(priority, JobInfo(ctr, callable, args...));
		}

		template <typename Callable, typename... Args>
		inline void waitForSingle(JobPriority priority, Callable callable, Args... args)
		{
			waitForSingle(priority, JobInfo(callable, args...));
		}

	protected:
		std::atomic_bool _shuttingDown = false;

		uint32_t _numThreads;
		Thread* _threads = nullptr;

		uint32_t _numFibers;
		Fiber* _fibers = nullptr;
		std::atomic_bool* _idleFibers = nullptr;

		FiberIndex findFreeFiber();
		void cleanupPreviousFiber(ThreadLocalStorage* tls = nullptr);

		ThreadLocalStorage* getCurrentTLS() const;

		helper::JobQueue _highPriorityQueue;
		helper::JobQueue _normalPriorityQueue;
		helper::JobQueue _lowPriorityQueue;

		helper::JobQueue* getQueueByPriority(JobPriority priority);
		bool getNextJob(JobInfo& info, ThreadLocalStorage* tls);

	private:
		MainType _mainCallback = nullptr;
		bool _shutdownAfterMain = true;

		static void threadCallbackWorker(Thread* thread);
		static void fiberCallbackWorker(Fiber* fiber);
		static void fiberCallbackMain(Fiber* fiber);
	};
}