#include "Timer.hpp"

template<typename Clock, typename Duration>
Timer<Clock, Duration>::Timer(std::string_view file, std::string_view function) : _startTime(Clock::now()), _file(file), _function(function) {}

template<typename Clock, typename Duration>
Timer<Clock, Duration>::~Timer() {
    std::cout << *this << " seconds\n";
}

template<typename Clock, typename Duration>
void Timer<Clock, Duration>::reset() {
    _startTime = Clock::now();
    _stopTime.reset();
}

template<typename Clock, typename Duration>
void Timer<Clock, Duration>::stop() {
    _stopTime = Clock::now();
}

template<typename Clock, typename Duration>
double Timer<Clock, Duration>::elapsed() const {
    if (_stopTime) {
        return std::chrono::duration_cast<Duration>(*_stopTime - _startTime).count();
    }
    else {
        return std::chrono::duration_cast<Duration>(Clock::now() - _startTime).count();
    }
}

template class Timer<>;