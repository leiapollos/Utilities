#include <Counter.hpp>
#include <Manager.hpp>
#include <ThreadLocalStorage.hpp>
#include "../Logger/Logger.hpp"

jobSystem::BaseCounter::BaseCounter(Manager* mgr, uint8_t numWaitingFibers, WaitingFiber* waitingFibers, std::atomic_bool* freeWaitingSlots) :
	_numWaitingFibers(numWaitingFibers),
	_waitingFibers(waitingFibers),
	_freeWaitingSlots(freeWaitingSlots),
	_manager(mgr)
{
}

jobSystem::Counter::Counter(Manager* mgr) :
	BaseCounter(mgr, MAX_WAITING, _implWaitingFibers, _implFreeWaitingSlots)
{
	for (std::atomic_bool& implFreeWaitingSlot : _implFreeWaitingSlots)
	{
		implFreeWaitingSlot.store(true);
	}
}

jobSystem::TinyCounter::TinyCounter(Manager* mgr) :
	BaseCounter(mgr, 1, &_waitingFiber, &_freeWaitingSlot)
{
	_freeWaitingSlot.store(true);
}

void jobSystem::BaseCounter::initWaitingFibers()
{
	for (uint8_t i = 0; i < _numWaitingFibers; i++) {
		_freeWaitingSlots[i].store(true);
	}
}

jobSystem::Count jobSystem::BaseCounter::increment(Count incrementAmount)
{
	const Count prevTarget = _counter.fetch_add(incrementAmount);
	checkWaitingFibers(prevTarget + incrementAmount);

	return prevTarget;
}

jobSystem::Count jobSystem::BaseCounter::decrement(Count by)
{
	Count prev = _counter.fetch_sub(by);
	checkWaitingFibers(prev - by);

	return prev;
}

jobSystem::Count jobSystem::BaseCounter::getValue() const
{
	return _counter.load(std::memory_order_seq_cst);
}

bool jobSystem::BaseCounter::addWaitingFiber(FiberIndex fiberIndex, Count targetValue, std::atomic_bool* fiberStored)
{
	for (uint8_t i = 0; i < _numWaitingFibers; i++) {
		bool expected = true;
		if (!std::atomic_compare_exchange_strong_explicit(&_freeWaitingSlots[i], &expected, false, std::memory_order_seq_cst, std::memory_order_relaxed)) {
			continue;
		}

		WaitingFiber* const slot = &_waitingFibers[i];
		slot->fiberIndex = fiberIndex;
		slot->fiberStored = fiberStored;
		slot->targetValue = targetValue;

		slot->inUse.store(false);

		const Count counter = _counter.load(std::memory_order_relaxed);
		if (slot->inUse.load(std::memory_order_acquire)) {
			return false;
		}

		if (slot->targetValue == counter) {
			expected = false;
			if (!std::atomic_compare_exchange_strong_explicit(&slot->inUse, &expected, true, std::memory_order_seq_cst, std::memory_order_relaxed)) {
				return false;
			}

			_freeWaitingSlots[i].store(true, std::memory_order_release);
			return true;
		}

		return false;
	}

	LOG(LogLevel::Critical, "Counter waiting slots are full!");
	return false;
}

void jobSystem::BaseCounter::checkWaitingFibers(Count value)
{
	for (size_t i = 0; i < _numWaitingFibers; i++) {
		if (_freeWaitingSlots[i].load(std::memory_order_acquire)) {
			continue;
		}

		WaitingFiber* const  waitingSlot = &_waitingFibers[i];
		if (waitingSlot->inUse.load(std::memory_order_acquire)) {
			continue;
		}

		if (waitingSlot->targetValue == value) {
			bool expected = false;
			if (!std::atomic_compare_exchange_strong_explicit(&waitingSlot->inUse, &expected, true, std::memory_order_seq_cst, std::memory_order_relaxed)) {
				continue;
			}

			_manager->getCurrentTLS()->readyFibers.emplace_back(waitingSlot->fiberIndex, waitingSlot->fiberStored);
			_freeWaitingSlots[i].store(true, std::memory_order_release);
		}
	}
}