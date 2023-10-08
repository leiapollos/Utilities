#pragma once
#include <vector>

#include "Counter.hpp"
#include "Job.hpp"

namespace jobSystem
{
	class Manager;
	class Counter;

	class JobQueue
	{
		Manager* _manager;
		JobPriority _defaultPriority;

		Counter _counter;
		std::vector<std::pair<JobPriority, JobInfo>> _queue;

	public:
		JobQueue(Manager* manager, JobPriority defaultPriority = JobPriority::Normal);
		~JobQueue() = default;

		void add(JobPriority priority, JobInfo job);

		inline void add(const JobInfo& job)
		{
			add(_defaultPriority, job);
		}

		template <typename... Args>
		inline void add(JobPriority priority, Args... args)
		{
			_queue.emplace_back(priority, JobInfo(&_counter, args...));
		}

		template <typename... Args>
		inline void Add(Args... args)
		{
			_queue.emplace_back(_defaultPriority, JobInfo(&_counter, args...));
		}

		JobQueue& operator+=(const JobInfo&);

		void execute();

		bool step();
	};
}