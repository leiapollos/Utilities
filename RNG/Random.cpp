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

int Random::generateWeightedIndex(const std::vector<double>& weights, EngineType engineType) {
    std::discrete_distribution<> dist{ weights.begin(), weights.end() };
    return generateNumber<int>(dist, engineType);
}
