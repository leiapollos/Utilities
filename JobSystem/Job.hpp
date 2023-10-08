#pragma once

#include "helper/MPMCBoundedQueue.hpp"
#include "helper/Delegate.hpp"
#include "helper/FunctionChecks.hpp"
#include "Counter.hpp"

namespace jobSystem
{
	class Counter;
	class BaseCounter;

	class JobInfo
	{
	public:
		JobInfo() = default;

		template <typename TCallable, typename... Args>
		JobInfo(Counter* ctr, TCallable callable, Args... args) : _counter(ctr)
		{
			reset();
			helper::FunctionChecker<TCallable, Args...>::check();
			storeJobInfo<typename helper::DelegateCallable<TCallable, Args...>>(callable, args...);
		}

		template <typename Ret, typename... Args>
		JobInfo(Counter* ctr, Ret(*function)(Args...), Args... args) : _counter(ctr)
		{
			reset();
			helper::FunctionChecker<decltype(function), Args...>::check();
			storeJobInfo<typename helper::DelegateCallable<decltype(function), Args...>>(function, args...);
		}

		template <class TCallable, typename... Args>
		JobInfo(Counter* ctr, TCallable* callable, Args... args) : _counter(ctr)
		{
			reset();
			helper::FunctionChecker<TCallable, Args...>::check();
			storeJobInfo(callable, args...);
		}

		template <class TClass, typename Ret, typename... Args>
		JobInfo(Counter* ctr, Ret(TClass::* callable)(Args...), TClass* inst, Args... args) : _counter(ctr)
		{
			reset();
			helper::FunctionChecker<decltype(callable), TClass*, Args...>::check();
			storeJobInfo<typename helper::DelegateMember<TClass, Ret, Args...>>(callable, inst, args...);
		}

		template <typename TCallable, typename... Args>
		JobInfo(TCallable callable, Args... args) : JobInfo((Counter*)nullptr, callable, args...) {}

		template <typename Ret, typename... Args>
		JobInfo(Ret(*function)(Args...), Args... args) : JobInfo((Counter*)nullptr, function, args...) {}

		template <class TCallable, typename... Args>
		JobInfo(TCallable* callable, Args... args) : JobInfo((Counter*)nullptr, callable, args...) {}

		template <class TClass, typename Ret, typename... Args>
		JobInfo(Ret(TClass::* callable)(Args...), TClass* inst, Args... args) : JobInfo((Counter*)nullptr, callable, inst, args...) {}

		~JobInfo();

		JobInfo& operator=(const JobInfo& other)
		{
			memcpy(_buffer, other._buffer, BufferSize);
			_counter = other._counter;
			return *this;
		}

		inline void setCounter(jobSystem::BaseCounter* ctr)
		{
			_counter = ctr;
		}

		inline jobSystem::BaseCounter* getCounter() const
		{
			return _counter;
		}

		void execute();

	private:
		static constexpr size_t BufferSize = sizeof(void*) * (8);

		char _buffer[BufferSize] = { 0 };
		void reset();


		inline helper::BaseDelegate* getDelegate()
		{
			return reinterpret_cast<helper::BaseDelegate*>(_buffer);
		}

		inline bool isNull() const
		{
			return *(void**)_buffer == nullptr;
		}

		template <typename TDelegate, typename... Args>
		void storeJobInfo(Args... args)
		{
			helper::DelegateSizeChecker<sizeof(TDelegate), BufferSize>::check();
			new(_buffer) TDelegate(args...);
		}

		template <class TClass, typename... Args>
		void storeJobInfo(TClass* inst, Args... args)
		{
			using Ret = std::invoke_result_t<decltype(&TClass::operator()), TClass*, Args...>;
			storeJobInfo<typename helper::DelegateMember<TClass, Ret, Args...>>(&TClass::operator(), inst, args...);
		}

		BaseCounter* _counter = nullptr;
	};

	enum class JobPriority : uint8_t
	{
		High,
		Normal,
		Low
	};

	namespace helper
	{
		using JobQueue = MPMCBoundedQueue<JobInfo>;
	}
}