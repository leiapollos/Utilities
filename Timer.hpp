#pragma once
#include <chrono>
#include <iostream>
#include <optional>
#include <string_view>

template<typename Clock = std::chrono::high_resolution_clock, typename Duration = std::chrono::duration<double>>
class Timer {
public:
    Timer(std::string_view file, std::string_view function);
    ~Timer();

    void reset();

    void stop();

    double elapsed() const;

    friend std::ostream& operator<<(std::ostream& os, const Timer& t) {
        os << "[" << t._file << " - " << t._function << "]: " << t.elapsed();
        return os;
    }

private:
    std::chrono::time_point<Clock> _startTime;
    std::optional<std::chrono::time_point<Clock>> _stopTime;
    std::string_view _file;
    std::string_view _function;
};

#define TIMER(name) Timer name(__FILE__, __func__)

extern template class Timer<>;