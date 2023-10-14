#pragma once
#include <type_traits>

namespace jobSystem
{
	namespace helper
	{
		template <typename TCallable, typename... Args>
		struct FunctionChecker
		{
			static constexpr void check()
			{
				constexpr bool value = std::is_invocable<TCallable, Args...>::value;
				static_assert(value, __FUNCTION__ ": Function is not callable <type>");
			}
		};

		template <unsigned Actual, unsigned Maximum>
		struct DelegateSizeChecker
		{
			static constexpr void check()
			{
				static_assert(Actual <= Maximum, __FUNCTION__ ": Delegate exceeds size limit <Actual Size, Maximum Size>");
			}
		};
	}
}