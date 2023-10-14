#include <Fiber.hpp>
#include "../Logger/Logger.hpp"
#ifdef _WIN32
#include <Windows.h>
#else
#error Only supported on Windows
#endif

static void launchFiber(jobSystem::Fiber* fiber)
{
	const jobSystem::FiberCallback callback = fiber->getCallback();
	if (callback == nullptr) {
		LOG(LogLevel::Critical, "LaunchFiber: callback is nullptr");
		return;
	}

	callback(fiber);
}

jobSystem::Fiber::Fiber()
{
#ifdef _WIN32
	_fiber = CreateFiber(0, (LPFIBER_START_ROUTINE)launchFiber, this);
	_thread_fiber = false;
#endif
}

jobSystem::Fiber::~Fiber()
{
#ifdef _WIN32
	if (_fiber && !_thread_fiber) {
		DeleteFiber(_fiber);
	}
#endif
}

void jobSystem::Fiber::convertThreadToFiber()
{
#ifdef _WIN32
	if (_fiber && !_thread_fiber) {
		DeleteFiber(_fiber);
	}

	_fiber = ConvertThreadToFiber(nullptr);
	_thread_fiber = true;
#endif
}

void jobSystem::Fiber::setCallback(FiberCallback callback)
{
	if (callback == nullptr) {
		LOG(LogLevel::Critical, "callback cannot be nullptr");
	}

	_callback = callback;
}

void jobSystem::Fiber::switchTo(jobSystem::Fiber* fiber, void* userData)
{
	if (fiber == nullptr || fiber->_fiber == nullptr) {
		LOG(LogLevel::Critical, "Invalid fiber (nullptr or invalid)");
		return;
	}

	fiber->_userData = userData;
	fiber->_return_fiber = this;

	SwitchToFiber(fiber->_fiber);
}

void jobSystem::Fiber::switchBack()
{
	if (_return_fiber && _return_fiber->_fiber) {
		SwitchToFiber(_return_fiber->_fiber);
	} else {
		LOG(LogLevel::Critical, "Unable to switch back from Fiber (none or invalid return fiber)");
	}
}