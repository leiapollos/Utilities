#include <Thread.hpp>
#include "../Logger/Logger.hpp"
#ifdef _WIN32
#include <Windows.h>
#else
#error Only supported on Windows
#endif

#ifdef _WIN32
static void WINAPI LaunchThread(void* ptr)
{
	const auto thread = reinterpret_cast<jobSystem::Thread*>(ptr);
	const jobSystem::ThreadCallback callback = thread->getCallback();

	if (callback == nullptr) {
		LOG(LogLevel::Critical, "LaunchThread: callback is nullptr");
		return;
	}

	thread->waitForReady();
	callback(thread);
}
#endif

bool jobSystem::Thread::spawn(ThreadCallback callback, void* userData)
{
	_handle = nullptr;
	_id = (std::numeric_limits<ThreadID>::max)();
	_callback = callback;
	_userData = userData;
	_cvReceivedID.notify_all();

#ifdef _WIN32
	{
		std::unique_lock lock(_startupIDMutex);
		_handle = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)LaunchThread, this, 0, (DWORD*)&_id);
	}
#endif

	return hasSpawned();
}

void jobSystem::Thread::setAffinity(size_t coreID)
{
#ifdef _WIN32
	if (!hasSpawned()) {
		return;
	}

	const DWORD_PTR mask = 1ull << coreID;
	SetThreadAffinityMask(_handle, mask);
#endif
}

void jobSystem::Thread::join()
{
	if (!hasSpawned()) {
		return;
	}

#ifdef _WIN32
	WaitForSingleObject(_handle, INFINITE);
#endif
}

void jobSystem::Thread::fromCurrentThread()
{
	_handle = GetCurrentThread();
	_id = GetCurrentThreadId();
}

void jobSystem::Thread::waitForReady()
{
	{
		std::unique_lock lock(_startupIDMutex);
		if (_id != (std::numeric_limits<ThreadID>::max)()) {
			return;
		}
	}

	std::mutex mutex;
	std::unique_lock lock(mutex);
	_cvReceivedID.wait(lock);
}

void jobSystem::Thread::sleepFor(uint32_t ms)
{
#ifdef _WIN32
	Sleep(ms);
#endif
}