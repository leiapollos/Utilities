#pragma once
#include <atomic>
#include <limits>

#include "JobSystemTypes.hpp"

namespace jobSystem
{
	class Manager;

	class BaseCounter
	{
		friend class Manager;

	protected:
		struct WaitingFiber;

	public:
		BaseCounter(Manager* mgr, uint8_t numWaitingFibers, WaitingFiber* waitingFibers, std::atomic_bool* freeWaitingSlots);
		virtual ~BaseCounter() = default;

		Count increment(Count by = 1);
		Count decrement(Count by = 1);

		Count getValue() const;

	protected:

		std::atomic<Count> _counter = 0;

		struct WaitingFiber
		{
			FiberIndex fiberIndex = (std::numeric_limits<FiberIndex>::max)();
			std::atomic_bool* fiberStored = nullptr;
			Count targetValue = 0;

			std::atomic_bool inUse = true;
		};

		const uint8_t _numWaitingFibers;
		WaitingFiber* _waitingFibers;
		std::atomic_bool* _freeWaitingSlots;

		void initWaitingFibers();

		Manager* _manager;

		bool addWaitingFiber(FiberIndex fiberIndex, Count targetValue, std::atomic_bool* fiberStored);
		void checkWaitingFibers(Count);
	};

	struct TinyCounter : public BaseCounter
	{
		TinyCounter(Manager*);
		~TinyCounter() = default;

		std::atomic_bool _freeWaitingSlot;
		WaitingFiber _waitingFiber;
	};

	class Counter :
		public BaseCounter
	{
	public:
		Counter(Manager*);
		~Counter() = default;

	private:
		static constexpr uint8_t MAX_WAITING = 5;
		std::atomic_bool _implFreeWaitingSlots[MAX_WAITING];
		WaitingFiber _implWaitingFibers[MAX_WAITING];
	};
}