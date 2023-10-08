#pragma once
#include "Counter.hpp"
#include "Job.hpp"
#include "Manager.hpp"

namespace jobSystem
{
	class Manager;
	class Counter;

	class JobList
	{
		Manager* _manager;
		JobPriority _defaultPriority;

		Counter _counter;

	public:
		JobList(Manager* manager, JobPriority defaultPriority = JobPriority::Normal);
		~JobList() = default;

		void add(JobPriority, JobInfo);

		inline void add(const JobInfo& job)
		{
			add(_defaultPriority, job);
		}

		template <typename... Args>
		inline void add(JobPriority priority, Args... args)
		{
			_manager->scheduleJob(priority, &_counter, args...);
		}

		template <typename... Args>
		inline void add(Args... args)
		{
			_manager->scheduleJob(_defaultPriority, &_counter, args...);
		}

		JobList& operator+=(const JobInfo&);

		void wait(uint32_t targetValue = 0);
	};
}