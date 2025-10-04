//
// Created by André Leite on 04/10/2025.
//

#pragma once

// ////////////////////////
// Xorshift64* RNG

struct XorShift {
    U64 state;
};

static XorShift xorshift_seed(U64 seed);
static U32 xorshift_next(XorShift* s);
static U32 xorshift_bounded(XorShift* s, U32 bound);
