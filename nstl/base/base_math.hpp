//
// Created by André Leite on 26/07/2025.
//

#pragma once

// ////////////////////////
// Trigonometry

#define PI_F32         3.14159265358979323846f
#define DEG_TO_RAD(d)  ((d) * (PI_F32 / 180.0f))
#define RAD_TO_DEG(r)  ((r) * (180.0f / PI_F32))

#define SIN_F32(v)     sinf(v)
#define COS_F32(v)     cosf(v)
#define TAN_F32(v)     tanf(v)

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
union Vec2F32 {
    struct {
        F32 x, y;
    };

    F32 v[2];
};

union Vec2S32 {
    struct {
        S32 x, y;
    };

    S32 v[2];
};

union Vec2S64 {
    struct {
        S64 x, y;
    };

    S64 v[2];
};

// Vec3

union Vec3F32 {
    struct {
        F32 x, y, z;
    };

    struct {
        F32 r, g, b;
    };

    F32 v[3];
};

union Vec3S32 {
    struct {
        S32 x, y, z;
    };

    struct {
        S32 r, g, b;
    };

    S32 v[3];
};

union Vec3S64 {
    struct {
        S64 x, y, z;
    };

    struct {
        S64 r, g, b;
    };

    S64 v[3];
};

// Vec4

union Vec4F32 {
    struct {
        F32 x, y, z, w;
    };

    struct {
        F32 r, g, b, a;
    };

    F32 v[4];
};

union Vec4S32 {
    struct {
        S32 x, y, z, w;
    };

    struct {
        S32 r, g, b, a;
    };

    S32 v[4];
};

union Vec4S64 {
    struct {
        S64 x, y, z, w;
    };

    struct {
        S64 r, g, b, a;
    };

    S64 v[4];
};


// ////////////////////////
// Matrix

struct Mat3x3F32 {
    F32 v[3][3];
};

struct Mat4x4F32 {
    F32 v[4][4];
};


// ////////////////////////
// Quaternion

union QuatF32 {
    struct {
        F32 x, y, z, w;
    };
    F32 v[4];
};


// ////////////////////////
// Matrix Operators

inline Mat4x4F32 operator*(const Mat4x4F32& a, const Mat4x4F32& b) noexcept {
    Mat4x4F32 res = {};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            res.v[row][col] = a.v[row][0] * b.v[0][col]
                            + a.v[row][1] * b.v[1][col]
                            + a.v[row][2] * b.v[2][col]
                            + a.v[row][3] * b.v[3][col];
        }
    }
    return res;
}

inline Vec4F32 operator*(const Mat4x4F32& m, const Vec4F32& v) noexcept {
    Vec4F32 res;
    res.x = m.v[0][0] * v.x + m.v[0][1] * v.y + m.v[0][2] * v.z + m.v[0][3] * v.w;
    res.y = m.v[1][0] * v.x + m.v[1][1] * v.y + m.v[1][2] * v.z + m.v[1][3] * v.w;
    res.z = m.v[2][0] * v.x + m.v[2][1] * v.y + m.v[2][2] * v.z + m.v[2][3] * v.w;
    res.w = m.v[3][0] * v.x + m.v[3][1] * v.y + m.v[3][2] * v.z + m.v[3][3] * v.w;
    return res;
}


// ////////////////////////
// Vector Operations


inline F32 vec3_dot(Vec3F32 a, Vec3F32 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3F32 vec3_cross(Vec3F32 a, Vec3F32 b) {
    Vec3F32 res;
    res.x = a.y * b.z - a.z * b.y;
    res.y = a.z * b.x - a.x * b.z;
    res.z = a.x * b.y - a.y * b.x;
    return res;
}

inline F32 vec3_length(Vec3F32 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Vec3F32 vec3_normalize(Vec3F32 v) {
    F32 len = vec3_length(v);
    if (len > 0.0f) {
        F32 inv = 1.0f / len;
        v.x *= inv;
        v.y *= inv;
        v.z *= inv;
    }
    return v;
}

// ////////////////////////
// Quaternion Operations

inline QuatF32 quat_from_axis_angle(Vec3F32 axis, F32 angleRadians) {
    F32 halfAngle = angleRadians * 0.5f;
    F32 sinHalf = sinf(halfAngle);
    F32 cosHalf = cosf(halfAngle);
    Vec3F32 normAxis = vec3_normalize(axis);
    QuatF32 q;
    q.x = normAxis.x * sinHalf;
    q.y = normAxis.y * sinHalf;
    q.z = normAxis.z * sinHalf;
    q.w = cosHalf;
    return q;
}

inline Mat4x4F32 quat_to_mat4(QuatF32 q) {
    Mat4x4F32 m = {};
    F32 xx = q.x * q.x;
    F32 yy = q.y * q.y;
    F32 zz = q.z * q.z;
    F32 xy = q.x * q.y;
    F32 xz = q.x * q.z;
    F32 yz = q.y * q.z;
    F32 wx = q.w * q.x;
    F32 wy = q.w * q.y;
    F32 wz = q.w * q.z;

    m.v[0][0] = 1.0f - 2.0f * (yy + zz);
    m.v[0][1] = 2.0f * (xy + wz);
    m.v[0][2] = 2.0f * (xz - wy);
    m.v[0][3] = 0.0f;

    m.v[1][0] = 2.0f * (xy - wz);
    m.v[1][1] = 1.0f - 2.0f * (xx + zz);
    m.v[1][2] = 2.0f * (yz + wx);
    m.v[1][3] = 0.0f;

    m.v[2][0] = 2.0f * (xz + wy);
    m.v[2][1] = 2.0f * (yz - wx);
    m.v[2][2] = 1.0f - 2.0f * (xx + yy);
    m.v[2][3] = 0.0f;

    m.v[3][0] = 0.0f;
    m.v[3][1] = 0.0f;
    m.v[3][2] = 0.0f;
    m.v[3][3] = 1.0f;

    return m;
}

inline QuatF32 quat_identity() {
    QuatF32 q;
    q.x = 0.0f;
    q.y = 0.0f;
    q.z = 0.0f;
    q.w = 1.0f;
    return q;
}

inline QuatF32 quat_mul(QuatF32 a, QuatF32 b) {
    QuatF32 q;
    q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return q;
}

inline QuatF32 quat_normalize(QuatF32 q) {
    F32 len = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 0.0f) {
        F32 inv = 1.0f / len;
        q.x *= inv;
        q.y *= inv;
        q.z *= inv;
        q.w *= inv;
    }
    return q;
}

inline QuatF32 quat_conjugate(QuatF32 q) {
    QuatF32 c;
    c.x = -q.x;
    c.y = -q.y;
    c.z = -q.z;
    c.w = q.w;
    return c;
}

// ////////////////////////
// Matrix Builders

inline Mat4x4F32 mat4_identity() {
    Mat4x4F32 m = {};
    m.v[0][0] = 1.0f;
    m.v[1][1] = 1.0f;
    m.v[2][2] = 1.0f;
    m.v[3][3] = 1.0f;
    return m;
}

inline Mat4x4F32 mat4_translate(Vec3F32 translation) {
    Mat4x4F32 m = mat4_identity();
    m.v[3][0] = translation.x;
    m.v[3][1] = translation.y;
    m.v[3][2] = translation.z;
    return m;
}

inline Mat4x4F32 mat4_scale(Vec3F32 scale) {
    Mat4x4F32 m = {};
    m.v[0][0] = scale.x;
    m.v[1][1] = scale.y;
    m.v[2][2] = scale.z;
    m.v[3][3] = 1.0f;
    return m;
}

inline Mat4x4F32 mat4_transpose(Mat4x4F32 m) {
    Mat4x4F32 t = {};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            t.v[i][j] = m.v[j][i];
        }
    }
    return t;
}

inline Mat4x4F32 mat4_perspective(F32 fovYRadians, F32 aspect, F32 zNear, F32 zFar) {
    Mat4x4F32 m = {};
    F32 tanHalfFov = tanf(fovYRadians * 0.5f);
    F32 f = 1.0f / tanHalfFov;

    m.v[0][0] = f / aspect;
    m.v[1][1] = -f;
    m.v[2][2] = zFar / (zNear - zFar);
    m.v[3][2] = (zNear * zFar) / (zNear - zFar);
    m.v[2][3] = -1.0f;

    return m;
}

inline Mat4x4F32 mat4_inverse(Mat4x4F32 m) {
    Mat4x4F32 inv = {};

    F32 a00 = m.v[0][0], a01 = m.v[0][1], a02 = m.v[0][2], a03 = m.v[0][3];
    F32 a10 = m.v[1][0], a11 = m.v[1][1], a12 = m.v[1][2], a13 = m.v[1][3];
    F32 a20 = m.v[2][0], a21 = m.v[2][1], a22 = m.v[2][2], a23 = m.v[2][3];
    F32 a30 = m.v[3][0], a31 = m.v[3][1], a32 = m.v[3][2], a33 = m.v[3][3];

    F32 b00 = a00 * a11 - a01 * a10;
    F32 b01 = a00 * a12 - a02 * a10;
    F32 b02 = a00 * a13 - a03 * a10;
    F32 b03 = a01 * a12 - a02 * a11;
    F32 b04 = a01 * a13 - a03 * a11;
    F32 b05 = a02 * a13 - a03 * a12;
    F32 b06 = a20 * a31 - a21 * a30;
    F32 b07 = a20 * a32 - a22 * a30;
    F32 b08 = a20 * a33 - a23 * a30;
    F32 b09 = a21 * a32 - a22 * a31;
    F32 b10 = a21 * a33 - a23 * a31;
    F32 b11 = a22 * a33 - a23 * a32;

    F32 det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;

    if (det == 0.0f) {
        return mat4_identity();
    }

    F32 invDet = 1.0f / det;

    inv.v[0][0] = ( a11 * b11 - a12 * b10 + a13 * b09) * invDet;
    inv.v[0][1] = (-a01 * b11 + a02 * b10 - a03 * b09) * invDet;
    inv.v[0][2] = ( a31 * b05 - a32 * b04 + a33 * b03) * invDet;
    inv.v[0][3] = (-a21 * b05 + a22 * b04 - a23 * b03) * invDet;
    inv.v[1][0] = (-a10 * b11 + a12 * b08 - a13 * b07) * invDet;
    inv.v[1][1] = ( a00 * b11 - a02 * b08 + a03 * b07) * invDet;
    inv.v[1][2] = (-a30 * b05 + a32 * b02 - a33 * b01) * invDet;
    inv.v[1][3] = ( a20 * b05 - a22 * b02 + a23 * b01) * invDet;
    inv.v[2][0] = ( a10 * b10 - a11 * b08 + a13 * b06) * invDet;
    inv.v[2][1] = (-a00 * b10 + a01 * b08 - a03 * b06) * invDet;
    inv.v[2][2] = ( a30 * b04 - a31 * b02 + a33 * b00) * invDet;
    inv.v[2][3] = (-a20 * b04 + a21 * b02 - a23 * b00) * invDet;
    inv.v[3][0] = (-a10 * b09 + a11 * b07 - a12 * b06) * invDet;
    inv.v[3][1] = ( a00 * b09 - a01 * b07 + a02 * b06) * invDet;
    inv.v[3][2] = (-a30 * b03 + a31 * b01 - a32 * b00) * invDet;
    inv.v[3][3] = ( a20 * b03 - a21 * b01 + a22 * b00) * invDet;

    return inv;
}

// ////////////////////////
// Operations

#define SQRT_F32(v)   sqrtf(v)



// ////////////////////////
// Min/Max

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))


// ////////////////////////
// Clamp

#define CLAMP_BOT(x, bottom)    MAX(x, bottom)
#define CLAMP_TOP(x, top)       MIN(x, top)
#define CLAMP(x, bottom, top)   MIN(MAX(x, bottom), top)


// ////////////////////////
// Memory Units

static constexpr U64 KB(U64 n) {
    return n << 10;
}

static constexpr U64 MB(U64 n) {
    return n << 20;
}

static constexpr U64 GB(U64 n) {
    return n << 30;
}

static constexpr U64 TB(U64 n) {
    return n << 40;
}


// ////////////////////////
// General Units

#define THOUSAND(n) (n * 1000)
#define MILLION(n)  (n * 1000000)
#define BILLION(n)  (n * 1000000000)

// ////////////////////////
// Time Units

#define SECONDS_TO_NANOSECONDS(s) ((s) * 1000000000ULL)
