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