#pragma once

namespace utils {
    template <typename T>
    struct default_deleter {
        void operator()(T* ptr) const {
            delete ptr;
        }
    };
}