#pragma once
#include <tuple>

namespace jobSystem
{
	namespace helper
	{
        struct BaseDelegate
        {
            virtual ~BaseDelegate() = default;
            virtual void call() = 0;
        };

        template <typename TCallable, typename... Args>
            requires std::invocable<TCallable, Args...>
        struct DelegateCallable : BaseDelegate
        {
            TCallable _callable;
            std::tuple<Args...> _args;

            DelegateCallable(TCallable callable, Args... args) : _callable(callable), _args(args...) {};

            virtual ~DelegateCallable() override = default;

            virtual void call() override
            {
                std::apply(_callable, _args);
            }
        };

        template <class TClass, typename Ret, typename... Args>
            requires std::is_member_function_pointer<Ret(TClass::*)(Args...)>::value
        struct DelegateMember : BaseDelegate
        {
            using function_t = Ret(TClass::*)(Args...);
            function_t _function;
            TClass* _instance;
            std::tuple<Args...> _args;

            DelegateMember(function_t function, TClass* inst, Args... args) :
                _function(function),
                _instance(inst),
                _args(args...)
            {}

            virtual ~DelegateMember() override = default;

            virtual void call() override
            {
                std::apply(_function, std::tuple_cat(std::make_tuple(_instance), _args));
            }
        };
	}
}