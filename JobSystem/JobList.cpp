#include <JobList.hpp>
#include <Manager.hpp>

jobSystem::JobList::JobList(Manager* mgr, JobPriority defaultPriority) :
	_manager(mgr),
	_defaultPriority(defaultPriority),
	_counter(mgr)
{}

void jobSystem::JobList::add(JobPriority priority, JobInfo job)
{
	job.setCounter(&_counter);

	_manager->scheduleJob(priority, job);
}

jobSystem::JobList& jobSystem::JobList::operator+=(const JobInfo& job)
{
	add(_defaultPriority, job);
	return *this;
}

void jobSystem::JobList::wait(uint32_t targetValue)
{
	_manager->waitForCounter(&_counter, targetValue);
}