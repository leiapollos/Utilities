#pragma once

#include "cpuid.h"
#include "typedefs.hpp"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <intrin.h>
#else
#error "Unsupported platrom"
#endif

namespace utils {
    namespace chrono
    {
        template<int64_t Num, int64_t Den>
        struct ratio {
            static const int64_t num = Num;
            static const int64_t den = Den;
        };

        template<typename Rep, class Period>
        class duration {
        public:
            typedef Rep rep;
            typedef Period period;

            explicit duration(rep c) : _count(c) {}
            duration() : _count(0) {}

            rep count() const {
                return _count;
            }

            duration& operator+=(const duration &o) {
                _count += o._count;
                return *this;
            }
            duration& operator-=(const duration &o) {
                _count -= o._count;
                return *this;
            }

            template<typename ToDuration>
            ToDuration to() const {
                long double s = (long double)_count
                                * ((long double)period::num / (long double)period::den)
                                * ((long double)ToDuration::period::den / (long double)ToDuration::period::num);
                return ToDuration((typename ToDuration::rep)s);
            }

        private:
            rep _count;
        };

        typedef duration<int64_t, ratio<1,1000000000>> nanoseconds;
        typedef duration<int64_t, ratio<1,1000000>>    microseconds;
        typedef duration<int64_t, ratio<1,1000>>       milliseconds;
        typedef duration<int64_t, ratio<1,1>>          seconds;

        template<class Clock, class Duration>
        class time_point {
        public:
            typedef Duration duration;
            typedef typename Duration::rep rep;
            typedef typename Duration::period period;
            typedef Clock clock;

            explicit time_point(const Duration &d) : _d(d) {}
            time_point() : _d(0) {}

            Duration time_since_epoch() const {
                return _d;
            }

            time_point& operator+=(const Duration &dur) {
                _d += dur;
                return *this;
            }
            time_point& operator-=(const Duration &dur) {
                _d -= dur;
                return *this;
            }

        private:
            Duration _d;
        };

        template<class Clock, class Duration>
        duration<typename Duration::rep, typename Duration::period>
        operator-(const time_point<Clock, Duration> &lhs,
                  const time_point<Clock, Duration> &rhs)
        {
            return duration<typename Duration::rep, typename Duration::period>(
                lhs.time_since_epoch().count() - rhs.time_since_epoch().count()
            );
        }

        template<class Clock, class Duration>
        time_point<Clock, Duration>
        operator+(const time_point<Clock, Duration> &tp, const Duration &d)
        {
            return time_point<Clock, Duration>(
                Duration(tp.time_since_epoch().count() + d.count())
            );
        }

        class hi_res_clock {
        public:
            typedef chrono::nanoseconds duration;
            typedef duration::rep rep;
            typedef duration::period period;
            typedef chrono::time_point<hi_res_clock, duration> time_point;

            static bool is_steady() {
                return true;
            }

            static void calibrate() {
                if (_calibrated) {
                    return;
                }
#if defined(ARCH_X64)
                _hasInvariantTsc = utils::cpu_id::info().isa.x86_info.invariantCounter;
                if (!_hasInvariantTsc) {
                    _cyclesPerNs = 0.0;
                    _calibrated = true;
                    _lastCalibrateNs = fallback_now_ns();
                    return;
                }
                do_calibration_hw();
#endif
            }

            static time_point now() {
                maybe_recalibrate();
#if defined(ARCH_X64)
                if (_calibrated && _cyclesPerNs > 0.0 && _hasInvariantTsc) {
                    unsigned int aux;
                    uint64_t c = __rdtscp(&aux);
                    long double ns = (long double)c / _cyclesPerNs;
                    return time_point(nanoseconds((int64_t)ns));
                }
#endif
                uint64_t fb = fallback_now_ns();
                return time_point(nanoseconds((int64_t)fb));
            }

        private:
            static void do_calibration_hw() {
#if defined(ARCH_X64)
                unsigned int aux_start, aux_end;
                uint64_t startCnt = __rdtscp(&aux_start);
#endif
                uint64_t startNs = fallback_now_ns();
                sleep_short();
#if defined(ARCH_X64)
                uint64_t endCnt = __rdtscp(&aux_end);
#endif
                uint64_t endNs = fallback_now_ns();

#if defined(ARCH_X64)
                if (aux_start != aux_end) {
                    // Cores differ, calibration invalid
                    _cyclesPerNs = 0.0;
                    _calibrated = true;
                    _lastCalibrateNs = endNs;
                    return;
                }
#endif

                uint64_t deltaCnt = (endCnt > startCnt) ? (endCnt - startCnt) : 1;
                uint64_t deltaNs = (endNs > startNs) ? (endNs - startNs) : 1;

                _cyclesPerNs = (long double)deltaCnt / (long double)deltaNs;
                _calibrated = true;
                _lastCalibrateNs = endNs;
            }

            static void maybe_recalibrate() {
                if (!_calibrated) {
                    calibrate();
                    return;
                }
                if (!_hasInvariantTsc || _cyclesPerNs <= 0.0) {
                    return;
                }
                uint64_t nowFb = fallback_now_ns();
                uint64_t dtNs = nowFb - _lastCalibrateNs;
                const uint64_t RecalInterval = 2000000000ULL;
                if (dtNs < RecalInterval) {
                    return;
                }
#if defined(ARCH_X64)
                unsigned int aux;
                uint64_t c = __rdtscp(&aux);
#endif
                long double currentNs = (long double)c / _cyclesPerNs;
                long double diff = (long double)nowFb - currentNs;
                if (diff < 0.0) {
                    diff = -diff;
                }
                const long double DRIFT_THRESH = 1000000.0L;
                if (diff > DRIFT_THRESH) {
                    do_calibration_hw();
                }
            }

            static uint64_t fallback_now_ns() {
#if defined(PLATFORM_WINDOWS)
                LARGE_INTEGER freq, counter;
                if (!QueryPerformanceFrequency(&freq)) {
                    return (uint64_t)(GetTickCount64() * 1000000ULL);
                }
                QueryPerformanceCounter(&counter);
                long double s = (long double)counter.QuadPart / (long double)freq.QuadPart;
                return (uint64_t)(s * 1.0e9);
#endif
            }

            static void sleep_short() {
#if defined(PLATFORM_WINDOWS)
                Sleep(1);
#endif
            }

            static bool        _calibrated;
            static bool        _hasInvariantTsc;
            static long double _cyclesPerNs;
            static uint64_t _lastCalibrateNs;
        };

        bool        hi_res_clock::_calibrated       = false;
        bool        hi_res_clock::_hasInvariantTsc  = false;
        long double hi_res_clock::_cyclesPerNs      = 0.0;
        uint64_t hi_res_clock::_lastCalibrateNs  = 0ULL;

    }
}