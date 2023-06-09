#pragma once

#include <random>
#include <vector>

class Random {
public:
    enum class EngineType { MersenneTwister, Xorshift, PCG };

    Random();

    void setSeed(unsigned seed = 0);
    template<typename T>
    T generate(T min, T max, EngineType engineType = EngineType::MersenneTwister);
    int generateWeightedIndex(const std::vector<double>& weights, EngineType engineType = EngineType::MersenneTwister);

private:
    template<typename T, typename Distribution>
    T generateNumber(Distribution& dist, EngineType engineType);

    static thread_local std::mt19937 _mt19937Engine;
    static thread_local std::default_random_engine _defaultEngine;
};

template<typename T>
T Random::generate(T min, T max, EngineType engineType) {
    static_assert(std::is_arithmetic<T>::value, "T must be a numeric type.");

    using Distribution = std::conditional_t<std::is_integral<T>::value,
        std::uniform_int_distribution<T>,
        std::uniform_real_distribution<T>>;
    Distribution dist{ min, max };

    return generateNumber<T>(dist, engineType);
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
