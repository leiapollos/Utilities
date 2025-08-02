//
// Created by André Leite on 26/07/2025.
//

#pragma once

// ////////////////////////
// Alignment

constexpr U64 align_pow2(U64 x, U64 a) {
    return (x + a - 1) & (~(a - 1));
}

constexpr B32 is_power_of_two(U64 x) {
    return (x > 0) && ((x & (x - 1)) == 0);
}


// ////////////////////////
// Vector

// Vec2
union Vec2f32 {
    struct { F32 x, y; };
    F32 v[2];
};

union Vec2i32 {
    struct { S32 x, y; };
    S32 v[2];
};

union Vec2i64 {
    struct { S64 x, y; };
    S64 v[2];
};

// Vec3

union Vec3f32 {
    struct { F32 x, y, z; };
    struct { F32 r, g, b; };
    F32 v[3];
};

union Vec3i32 {
    struct { S32 x, y, z; };
    struct { S32 r, g, b; };
    S32 v[3];
};

union Vec3i64 {
    struct { S64 x, y, z; };
    struct { S64 r, g, b; };
    S64 v[3];
};

// Vec4

union Vec4f32 {
    struct { F32 x, y, z, w; };
    struct { F32 r, g, b, a; };
    F32 v[4];
};

union Vec4i32 {
    struct { S32 x, y, z, w; };
    struct { S32 r, g, b, a; };
    S32 v[4];
};

union Vec4i64 {
    struct { S64 x, y, z, w; };
    struct { S64 r, g, b, a; };
    S64 v[4];
};


// ////////////////////////
// Matrix

struct Mat3x3f32 {
    F32 v[3][3];
};

struct Mat4x4f32 {
    F32 v[4][4];
};


// ////////////////////////
// Operations

#define sqrt_f32(v)   sqrtf(v)


// ////////////////////////
// Min/Max

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))


// ////////////////////////
// Clamp

#define CLAMP_BOT(x, bottom) MAX(x, bottom)
#define CLAMP_TOP(x, top)    MIN(x, top)


// ////////////////////////
// Memory Units

static constexpr U64 KB(U64 n) { return n << 10; }
static constexpr U64 MB(U64 n) { return n << 20; }
static constexpr U64 GB(U64 n) { return n << 30; }
static constexpr U64 TB(U64 n) { return n << 40; }


// ////////////////////////
// General Units

#define THOUSAND(n) (n * 1000)
#define MILLION(n)  (n * 1000000)