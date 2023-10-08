#include <Job.hpp>
#include <Counter.hpp>

jobSystem::JobInfo::~JobInfo()
{
	reset();
}

void jobSystem::JobInfo::execute()
{
	if (!isNull()) {
		getDelegate()->call();
	}

	if (_counter) {
		_counter->decrement();
	}
}

void jobSystem::JobInfo::reset()
{
	if (!isNull())
	{
		getDelegate()->~BaseDelegate();
		*(void**)_buffer = nullptr;
	}
}