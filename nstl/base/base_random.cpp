//
// Created by André Leite on 04/10/2025.
//

static
XorShift xorshift_seed(U64 seed) {
    XorShift s{};
    s.state = seed ? seed : 0x9E3779B97F4A7C15ull; // avoid zero state
    return s;
}

static
U32 xorshift_next(XorShift* s) {
    U64 x = s->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    s->state = x;
    return (U32)((x * 2685821657736338717ull) >> 32);
}

static
U32 xorshift_bounded(XorShift* s, U32 bound) {
    return bound ? (xorshift_next(s) % bound) : 0;
}
