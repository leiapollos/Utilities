#pragma once
#include <cstdint>

namespace jobSystem
{
	class Fiber;
	using FiberCallback = void(*)(Fiber*);
	using FiberIndex = uint16_t;

	class Thread;
	using ThreadCallback = void(*)(Thread*);
	using ThreadID = uint32_t;

	using Count = uint32_t;
}
