//
// Created by André Leite on 09/06/2025.
//

namespace nstl {
    template<typename T>
    const T& min(const T& a, const T& b) {
        return (b < a) ? b : a;
    }

    template<typename T>
    const T& max(const T& a, const T& b) {
        return (a < b) ? b : a;
    }
}
