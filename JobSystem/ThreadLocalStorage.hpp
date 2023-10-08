#pragma once
#include <cstdint>
#include <vector>
#include <atomic>

#include "Fiber.hpp"
#include "JobSystemTypes.hpp"

namespace jobSystem
{
	enum class FiberLocation : uint8_t
	{
		None,
		Waiting,
		Pool
	};

	struct ThreadLocalStorage
	{
		ThreadLocalStorage() = default;
		~ThreadLocalStorage() = default;

		ThreadID threadIndex = (std::numeric_limits<ThreadID>::max)();

		Fiber threadFiber;

		FiberIndex currentFiberIndex = (std::numeric_limits<FiberIndex>::max)();

		FiberIndex previousFiberIndex = (std::numeric_limits<FiberIndex>::max)();
		std::atomic_bool* previousFiberStored = nullptr;
		FiberLocation previousFiberLocation = FiberLocation::None;

		std::vector<std::pair<uint16_t, std::atomic_bool*>> readyFibers;
	};
}
