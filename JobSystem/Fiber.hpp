#pragma once
#include "JobSystemTypes.hpp"

namespace jobSystem
{
	class Fiber
	{
	public:
		Fiber();
		Fiber(const Fiber&) = delete;
		Fiber(const Fiber&&) = delete;
		~Fiber();

		void convertThreadToFiber();

		void setCallback(FiberCallback callback);

		void switchTo(Fiber* fiber, void* userData = nullptr);
		void switchBack();

		inline FiberCallback getCallback() const
		{
			return _callback;
		};

		inline void* getUserData() const
		{
			return _userData;
		};

		inline bool isValid() const
		{
			return _fiber && _callback;
		};

	private:
		Fiber(void* fiber) : _fiber(fiber) {}

		void* _fiber = nullptr;
		bool _thread_fiber = false;

		Fiber* _return_fiber = nullptr;

		FiberCallback _callback = nullptr;
		void* _userData = nullptr;
	};
}
