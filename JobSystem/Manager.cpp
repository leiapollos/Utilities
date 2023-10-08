#include <thread>
#include <limits>

#include <Manager.hpp>
#include <Thread.hpp>
#include <Fiber.hpp>

#include "../Logger/Logger.hpp"

jobSystem::Manager::Manager(const ManagerOptions& options) :
	_numThreads(options.numThreads),
	_numFibers(options.numFibers),
	_highPriorityQueue(options.highPriorityQueueSize),
	_normalPriorityQueue(options.normalPriorityQueueSize),
	_lowPriorityQueue(options.lowPriorityQueueSize),
	_shutdownAfterMain(options.shutdownAfterMainCallback)
{}

jobSystem::Manager::Manager(MainType main, const ManagerOptions& options) :
	Manager(options)
{
	run(main);
}

jobSystem::Manager::~Manager()
{
	delete[] _threads;
	delete[] _fibers;
	delete[] _idleFibers;
}

void jobSystem::Manager::run(MainType main)
{
	if (_threads || _fibers) {
		LOG(LogLevel::Critical, "System already initialized");
	}

	_threads = new Thread[_numThreads];

	Thread* mainThread = &_threads[0];
	mainThread->fromCurrentThread();
	
	mainThread->setAffinity(0);
	
	ThreadLocalStorage* mainThreadTLS = mainThread->getTLS();
	mainThreadTLS->threadFiber.convertThreadToFiber();

	if (_numFibers == 0) {
		LOG(LogLevel::Critical, "Invalid number of fibers. It should be greater than 0");
	}

	_fibers = new Fiber[_numFibers];
	_idleFibers = new std::atomic_bool[_numFibers];

	for (uint16_t i = 0; i < _numFibers; i++)
	{
		_fibers[i].setCallback(fiberCallbackWorker);
		_idleFibers[i].store(true, std::memory_order_relaxed);
	}

	if (_numThreads > std::thread::hardware_concurrency()) {
		LOG(LogLevel::Critical, "Cannot set thread affinity if the number of threads is greater than the number of logical cores in the system");
	}

	for (ThreadID i = 0; i < _numThreads; i++)
	{
		ThreadLocalStorage* threadTLS = _threads[i].getTLS();
		threadTLS->threadIndex = i;

		if (i > 0)
		{
			if (!_threads[i].spawn(threadCallbackWorker, this)) {
				LOG(LogLevel::Critical, "Error while spawning threads");
			}
		}
	}

	if (main == nullptr) {
		LOG(LogLevel::Critical, "The callback for the main thread cannot be null");
	}

	_mainCallback = main;

	mainThreadTLS->currentFiberIndex = findFreeFiber();
	Fiber* mainFiber = &_fibers[mainThreadTLS->currentFiberIndex];
	mainFiber->setCallback(fiberCallbackMain);

	mainThreadTLS->threadFiber.switchTo(mainFiber, this);

	for (uint8_t i = 1; i < _numThreads; i++) {
		_threads[i].join();
	}
}

void jobSystem::Manager::shutdown(bool blocking)
{
	_shuttingDown.store(true, std::memory_order_release);

	if (blocking)
	{
		for (uint8_t i = 1; i < _numThreads; i++) {
			_threads[i].join();
		}
	}
}

uint16_t jobSystem::Manager::findFreeFiber()
{
	while (true)
	{
		for (uint16_t i = 0; i < _numFibers; i++)
		{
			if (!_idleFibers[i].load(std::memory_order_relaxed) ||
				!_idleFibers[i].load(std::memory_order_acquire)) {
				continue;
			}

			bool expected = true;
			if (std::atomic_compare_exchange_weak_explicit(&_idleFibers[i], &expected, false, std::memory_order_release, std::memory_order_relaxed)) {
				return i;
			}
		}

		LOG(LogLevel::Warning, "No free fiber found!");
	}
}

void jobSystem::Manager::cleanupPreviousFiber(ThreadLocalStorage* tls)
{
	if (tls == nullptr) {
		tls = getCurrentTLS();
	}

	switch (tls->previousFiberLocation)
	{
	case FiberLocation::None:
		return;

	case FiberLocation::Pool:
		_idleFibers[tls->previousFiberIndex].store(true, std::memory_order_release);
		break;

	case FiberLocation::Waiting:
		tls->previousFiberStored->store(true, std::memory_order_relaxed);
		break;
	}
	
	tls->previousFiberIndex = (std::numeric_limits<FiberIndex>::max)();
	tls->previousFiberLocation = FiberLocation::None;
	tls->previousFiberStored = nullptr;
}

jobSystem::ThreadLocalStorage* jobSystem::Manager::getCurrentTLS() const
{
#ifdef _WIN32
	const uint32_t idx = GetCurrentThreadId();
	for (ThreadID i = 0; i < _numThreads; i++) {
		if (_threads[i].getID() == idx) {
			return _threads[i].getTLS();
		}
	}
#endif

	return nullptr;
}

void jobSystem::Manager::threadCallbackWorker(Thread* thread)
{
	const auto manager = reinterpret_cast<Manager*>(thread->getUserData());
	ThreadLocalStorage* tls = thread->getTLS();

	thread->setAffinity(tls->threadIndex);

	tls->threadFiber.convertThreadToFiber();

	tls->currentFiberIndex = manager->findFreeFiber();

	Fiber* fiber = &manager->_fibers[tls->currentFiberIndex];
	tls->threadFiber.switchTo(fiber, manager);
}

void jobSystem::Manager::fiberCallbackMain(Fiber* fiber)
{
	const auto manager = reinterpret_cast<Manager*>(fiber->getUserData());

	manager->_mainCallback(manager);

	if (!manager->_shutdownAfterMain) {
		ThreadLocalStorage* tls = manager->getCurrentTLS();
		tls->currentFiberIndex = manager->findFreeFiber();

		Fiber* currentFiber = &manager->_fibers[tls->currentFiberIndex];
		tls->threadFiber.switchTo(currentFiber, manager);
	}

	manager->shutdown(false);

	fiber->switchBack();
}

void jobSystem::Manager::fiberCallbackWorker(Fiber* fiber)
{
	const auto manager = reinterpret_cast<Manager*>(fiber->getUserData());
	manager->cleanupPreviousFiber();

	JobInfo job;

	while (!manager->isShuttingDown()) {
		ThreadLocalStorage* tls = manager->getCurrentTLS();

		if (manager->getNextJob(job, tls)) {
			job.execute();
			continue;
		}

		Thread::sleepFor(1);
	}

	fiber->switchBack();
}