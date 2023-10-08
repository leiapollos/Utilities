#include "Manager.hpp"
#include "Counter.hpp"
#include "ThreadLocalStorage.hpp"
#include "../Logger/Logger.hpp"

jobSystem::helper::JobQueue* jobSystem::Manager::getQueueByPriority(JobPriority priority)
{
	switch (priority)
	{
	case JobPriority::High:
		return &_highPriorityQueue;

	case JobPriority::Normal:
		return &_normalPriorityQueue;

	case JobPriority::Low:
		return &_lowPriorityQueue;
	}

	return nullptr;
}

bool jobSystem::Manager::getNextJob(JobInfo& job, ThreadLocalStorage* tls)
{
	if (_highPriorityQueue.dequeue(job)) {
		return true;
	}

	if (tls == nullptr) {
		tls = getCurrentTLS();
	}

	for (auto it = tls->readyFibers.begin(); it != tls->readyFibers.end(); ++it)
	{
		const FiberIndex fiberIndex = it->first;

		if (!it->second->load(std::memory_order_relaxed))
			continue;

		delete it->second;
		tls->readyFibers.erase(it);

		tls->previousFiberIndex = tls->currentFiberIndex;
		tls->previousFiberLocation = FiberLocation::Pool;
		tls->currentFiberIndex = fiberIndex;

		tls->threadFiber.switchTo(&_fibers[fiberIndex], this);
		cleanupPreviousFiber(tls);

		break;
	}

	return
		_normalPriorityQueue.dequeue(job) ||
		_lowPriorityQueue.dequeue(job);
}

void jobSystem::Manager::scheduleJob(JobPriority priority, const JobInfo& job)
{
	helper::JobQueue* const queue = getQueueByPriority(priority);
	if (!queue) {
		return;
	}

	if (job.getCounter()) {
		job.getCounter()->increment();
	}

	if (!queue->enqueue(job)) {
		LOG(LogLevel::Critical, "Job Queue is full!");
	}
}

void jobSystem::Manager::waitForCounter(BaseCounter* counter, uint32_t targetValue)
{
	if (counter == nullptr || counter->getValue() == targetValue) {
		return;
	}

	ThreadLocalStorage* const tls = getCurrentTLS();
	const auto fiberStored = new std::atomic_bool(false);

	if (counter->addWaitingFiber(tls->currentFiberIndex, targetValue, fiberStored)) {
		delete fiberStored;
		return;
	}

	tls->previousFiberIndex = tls->currentFiberIndex;
	tls->previousFiberLocation = FiberLocation::Waiting;
	tls->previousFiberStored = fiberStored;

	tls->currentFiberIndex = findFreeFiber();
	tls->threadFiber.switchTo(&_fibers[tls->currentFiberIndex], this);

	cleanupPreviousFiber();
}

void jobSystem::Manager::waitForSingle(JobPriority priority, JobInfo info)
{
	TinyCounter ctr(this);
	info.setCounter(&ctr);

	scheduleJob(priority, info);
	waitForCounter(&ctr);
}