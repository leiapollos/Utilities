#include <JobQueue.hpp>
#include <Manager.hpp>

jobSystem::JobQueue::JobQueue(jobSystem::Manager* mgr, JobPriority defaultPriority) :
	_manager(mgr),
	_defaultPriority(defaultPriority),
	_counter(mgr)
{}

void jobSystem::JobQueue::add(JobPriority priority, JobInfo job)
{
	job.setCounter(&_counter);
	_queue.emplace_back(priority, job);
}

jobSystem::JobQueue& jobSystem::JobQueue::operator+=(const JobInfo& job)
{
	add(_defaultPriority, job);
	return *this;
}

bool jobSystem::JobQueue::step()
{
	if (_queue.empty()) {
		return false;
	}

	const auto& job = _queue.front();
	_manager->scheduleJob(job.first, job.second);
	_manager->waitForCounter(&_counter);

	_queue.erase(_queue.begin());
	return true;
}

void jobSystem::JobQueue::execute()
{
	while (step()) {}
}