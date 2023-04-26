#include "Random.hpp"

#include <stdexcept>
#include <iostream>
#include <type_traits>
#include <limits>

thread_local std::mt19937 Random::_mt19937Engine;
thread_local std::default_random_engine Random::_defaultEngine;

Random::Random() {
    setSeed(std::random_device{}());
}

void Random::setSeed(unsigned seed) {
    _mt19937Engine.seed(seed);
    _defaultEngine.seed(seed);
}

template<typename T>
T Random::generate(T min, T max, EngineType engineType) {
    static_assert(std::is_arithmetic<T>::value, "T must be a numeric type.");

    using Distribution = std::conditional_t<std::is_integral<T>::value,
        std::uniform_int_distribution<T>,
        std::uniform_real_distribution<T>>;
    Distribution dist{ min, max };

    return generateNumber<T>(dist, engineType);
}

int Random::generateWeightedIndex(const std::vector<double>& weights, EngineType engineType) {
    std::discrete_distribution<> dist{ weights.begin(), weights.end() };
    return generateNumber<int>(dist, engineType);
}

template<typename T, typename Distribution>
T Random::generateNumber(Distribution& dist, EngineType engineType) {
    switch (engineType) {
        case EngineType::MersenneTwister: {
            return dist(_mt19937Engine);
        }
        case EngineType::PCG: {
            return dist(_defaultEngine);
        }
        default: {
            throw std::runtime_error("Invalid engine type.");
        }
    }
}