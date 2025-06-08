//
// Created by André Leite on 08/06/2025.
//

#pragma once

namespace nstl {
    class Function {
    public:
        Function() : _callable(nullptr) {}

        template <typename F>
        Function(const F& f) : _callable(new CallableImpl<F>(f)) {}

        Function(const Function& other)
            : _callable(other._callable ? other._callable->clone() : nullptr) {}

        Function(Function&& other) noexcept : _callable(other._callable) {
            other._callable = nullptr;
        }

        ~Function() { delete _callable; }

        Function& operator=(const Function& other) {
            if (this != &other) {
                delete _callable;
                _callable =
                    other._callable ? other._callable->clone() : nullptr;
            }
            return *this;
        }

        Function& operator=(Function&& other) noexcept {
            if (this != &other) {
                delete _callable;
                _callable = other._callable;
                other._callable = nullptr;
            }
            return *this;
        }

        void operator()() const {
            if (_callable) {
                _callable->invoke();
            }
        }

        explicit operator bool() const { return _callable != nullptr; }

    private:
        struct CallableBase {
            virtual ~CallableBase() = default;
            virtual void invoke() = 0;
            [[nodiscard]] virtual CallableBase* clone() const = 0;
        };

        template <typename F>
        struct CallableImpl final : public CallableBase {
            F _functor;
            explicit CallableImpl(const F& f) : _functor(f) {}
            virtual void invoke() override { _functor(); }
            [[nodiscard]] virtual CallableBase* clone() const override {
                return new CallableImpl<F>(_functor);
            }
        };

        CallableBase* _callable;
    };
}