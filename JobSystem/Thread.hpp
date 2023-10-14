#pragma once
#include <mutex>
#include <condition_variable>

#include "ThreadLocalStorage.hpp"
#include "JobSystemTypes.hpp"

namespace jobSystem
{
	class Thread
	{
	public:
		Thread() = default;
		Thread(const Thread&) = delete;
		Thread(const Thread&&) = delete;
		virtual ~Thread() = default;

		bool spawn(ThreadCallback callback, void* userData = nullptr);
		void setAffinity(size_t coreID);
		void join();
		void fromCurrentThread();


		inline ThreadLocalStorage* getTLS()
		{
			return &_tls;
		}

		inline ThreadCallback getCallback() const
		{
			return _callback;
		}

		inline void* getUserData() const
		{
			return _userData;
		}

		inline bool hasSpawned() const
		{
			return _id != (std::numeric_limits<ThreadID>::max)();
		}

		inline const ThreadID getID() const
		{
			return _id;
		}

		void waitForReady();

		static void sleepFor(uint32_t ms);

	private:
		void* _handle = nullptr;
		ThreadID _id = (std::numeric_limits<ThreadID>::max)();
		ThreadLocalStorage _tls;

		std::condition_variable _cvReceivedID;
		std::mutex _startupIDMutex;

		ThreadCallback _callback = nullptr;
		void* _userData = nullptr;

		Thread(void* h, ThreadID id) : _handle(h), _id(id) {}
	};
}