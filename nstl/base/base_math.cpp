//
// Created by André Leite on 26/07/2025.
//

// --- Vec2F32 Operators ---

static
Vec2F32& operator+=(Vec2F32& a, const Vec2F32& b) noexcept {
    a.x += b.x;
    a.y += b.y;
    return a;
}

static
Vec2F32 operator+(Vec2F32 a, const Vec2F32& b) noexcept {
    return a += b;
}

static
Vec2F32& operator-=(Vec2F32& a, const Vec2F32& b) noexcept {
    a.x -= b.x;
    a.y -= b.y;
    return a;
}

static
Vec2F32 operator-(Vec2F32 a, const Vec2F32& b) noexcept {
    return a -= b;
}

static
Vec2F32& operator*=(Vec2F32& v, F32 s) noexcept {
    v.x *= s;
    v.y *= s;
    return v;
}

static
Vec2F32 operator*(Vec2F32 v, F32 s) noexcept {
    return v *= s;
}

static
Vec2F32 operator*(F32 s, Vec2F32 v) noexcept {
    return v * s;
}

static
Vec2F32& operator/=(Vec2F32& v, F32 s) noexcept {
    F32 inv = 1.0f / s;
    v.x *= inv;
    v.y *= inv;
    return v;
}

static
Vec2F32 operator/(Vec2F32 v, F32 s) noexcept {
    return v /= s;
}


// --- Vec3F32 Operators ---

static
Vec3F32& operator+=(Vec3F32& a, const Vec3F32& b) noexcept {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

static
Vec3F32 operator+(Vec3F32 a, const Vec3F32& b) noexcept {
    return a += b;
}

static
Vec3F32& operator-=(Vec3F32& a, const Vec3F32& b) noexcept {
    a.x -= b.x;
    a.y -= b.y;
    a.z -= b.z;
    return a;
}

static
Vec3F32 operator-(Vec3F32 a, const Vec3F32& b) noexcept {
    return a -= b;
}

static
Vec3F32& operator*=(Vec3F32& v, F32 s) noexcept {
    v.x *= s;
    v.y *= s;
    v.z *= s;
    return v;
}

static
Vec3F32 operator*(Vec3F32 v, F32 s) noexcept {
    return v *= s;
}

static
Vec3F32 operator*(F32 s, Vec3F32 v) noexcept {
    return v * s;
}

static
Vec3F32& operator/=(Vec3F32& v, F32 s) noexcept {
    F32 inv = 1.0f / s;
    v.x *= inv;
    v.y *= inv;
    v.z *= inv;
    return v;
}

static
Vec3F32 operator/(Vec3F32 v, F32 s) noexcept {
    return v /= s;
}


// --- Vec4F32 Operators ---

static
Vec4F32& operator+=(Vec4F32& a, const Vec4F32& b) noexcept {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    a.w += b.w;
    return a;
}

static
Vec4F32 operator+(Vec4F32 a, const Vec4F32& b) noexcept {
    return a += b;
}

static
Vec4F32& operator-=(Vec4F32& a, const Vec4F32& b) noexcept {
    a.x -= b.x;
    a.y -= b.y;
    a.z -= b.z;
    a.w -= b.w;
    return a;
}

static
Vec4F32 operator-(Vec4F32 a, const Vec4F32& b) noexcept {
    return a -= b;
}

static
Vec4F32& operator*=(Vec4F32& v, F32 s) noexcept {
    v.x *= s;
    v.y *= s;
    v.z *= s;
    v.w *= s;
    return v;
}

static
Vec4F32 operator*(Vec4F32 v, F32 s) noexcept {
    return v *= s;
}

static
Vec4F32 operator*(F32 s, Vec4F32 v) noexcept {
    return v * s;
}

static
Vec4F32& operator/=(Vec4F32& v, F32 s) noexcept {
    F32 inv = 1.0f / s;
    v.x *= inv;
    v.y *= inv;
    v.z *= inv;
    v.w *= inv;
    return v;
}

static
Vec4F32 operator/(Vec4F32 v, F32 s) noexcept {
    return v /= s;
}


// --- Mat3x3F32 Operators ---

static
Mat3x3F32& operator+=(Mat3x3F32& a, const Mat3x3F32& b) noexcept {
    a.v[0][0] += b.v[0][0]; a.v[0][1] += b.v[0][1]; a.v[0][2] += b.v[0][2];
    a.v[1][0] += b.v[1][0]; a.v[1][1] += b.v[1][1]; a.v[1][2] += b.v[1][2];
    a.v[2][0] += b.v[2][0]; a.v[2][1] += b.v[2][1]; a.v[2][2] += b.v[2][2];
    return a;
}

static
Mat3x3F32 operator+(Mat3x3F32 a, const Mat3x3F32& b) noexcept {
    return a += b;
}

static
Mat3x3F32& operator-=(Mat3x3F32& a, const Mat3x3F32& b) noexcept {
    a.v[0][0] -= b.v[0][0]; a.v[0][1] -= b.v[0][1]; a.v[0][2] -= b.v[0][2];
    a.v[1][0] -= b.v[1][0]; a.v[1][1] -= b.v[1][1]; a.v[1][2] -= b.v[1][2];
    a.v[2][0] -= b.v[2][0]; a.v[2][1] -= b.v[2][1]; a.v[2][2] -= b.v[2][2];
    return a;
}

static
Mat3x3F32 operator-(Mat3x3F32 a, const Mat3x3F32& b) noexcept {
    return a -= b;
}

static
Mat3x3F32& operator*=(Mat3x3F32& a, const Mat3x3F32& b) noexcept {
    Mat3x3F32 res{};
    res.v[0][0] = a.v[0][0] * b.v[0][0] + a.v[0][1] * b.v[1][0] + a.v[0][2] * b.v[2][0];
    res.v[0][1] = a.v[0][0] * b.v[0][1] + a.v[0][1] * b.v[1][1] + a.v[0][2] * b.v[2][1];
    res.v[0][2] = a.v[0][0] * b.v[0][2] + a.v[0][1] * b.v[1][2] + a.v[0][2] * b.v[2][2];
    res.v[1][0] = a.v[1][0] * b.v[0][0] + a.v[1][1] * b.v[1][0] + a.v[1][2] * b.v[2][0];
    res.v[1][1] = a.v[1][0] * b.v[0][1] + a.v[1][1] * b.v[1][1] + a.v[1][2] * b.v[2][1];
    res.v[1][2] = a.v[1][0] * b.v[0][2] + a.v[1][1] * b.v[1][2] + a.v[1][2] * b.v[2][2];
    res.v[2][0] = a.v[2][0] * b.v[0][0] + a.v[2][1] * b.v[1][0] + a.v[2][2] * b.v[2][0];
    res.v[2][1] = a.v[2][0] * b.v[0][1] + a.v[2][1] * b.v[1][1] + a.v[2][2] * b.v[2][1];
    res.v[2][2] = a.v[2][0] * b.v[0][2] + a.v[2][1] * b.v[1][2] + a.v[2][2] * b.v[2][2];
    a = res;
    return a;
}

static
Mat3x3F32 operator*(Mat3x3F32 a, const Mat3x3F32& b) noexcept {
    return a *= b;
}

static
Mat3x3F32& operator*=(Mat3x3F32& m, F32 s) noexcept {
    m.v[0][0] *= s; m.v[0][1] *= s; m.v[0][2] *= s;
    m.v[1][0] *= s; m.v[1][1] *= s; m.v[1][2] *= s;
    m.v[2][0] *= s; m.v[2][1] *= s; m.v[2][2] *= s;
    return m;
}

static
Mat3x3F32 operator*(Mat3x3F32 m, F32 s) noexcept {
    return m *= s;
}

static
Mat3x3F32 operator*(F32 s, Mat3x3F32 m) noexcept {
    return m * s;
}

static
Mat3x3F32& operator/=(Mat3x3F32& m, F32 s) noexcept {
    F32 inv = 1.0f / s;
    m.v[0][0] *= inv; m.v[0][1] *= inv; m.v[0][2] *= inv;
    m.v[1][0] *= inv; m.v[1][1] *= inv; m.v[1][2] *= inv;
    m.v[2][0] *= inv; m.v[2][1] *= inv; m.v[2][2] *= inv;
    return m;
}

static
Mat3x3F32 operator/(Mat3x3F32 m, F32 s) noexcept {
    return m /= s;
}


// --- Matrix-Vector Multiplication ---

static
Vec3F32 operator*(const Mat3x3F32& m, const Vec3F32& v) noexcept {
    Vec3F32 res;
    res.x = m.v[0][0] * v.x + m.v[0][1] * v.y + m.v[0][2] * v.z;
    res.y = m.v[1][0] * v.x + m.v[1][1] * v.y + m.v[1][2] * v.z;
    res.z = m.v[2][0] * v.x + m.v[2][1] * v.y + m.v[2][2] * v.z;
    return res;
}

static
Vec3F32 operator*(const Vec3F32& v, const Mat3x3F32& m) noexcept {
    Vec3F32 res;
    res.x = v.x * m.v[0][0] + v.y * m.v[1][0] + v.z * m.v[2][0];
    res.y = v.x * m.v[0][1] + v.y * m.v[1][1] + v.z * m.v[2][1];
    res.z = v.x * m.v[0][2] + v.y * m.v[1][2] + v.z * m.v[2][2];
    return res;
}


// --- Mat4x4F32 Operators ---

static
Mat4x4F32& operator+=(Mat4x4F32& a, const Mat4x4F32& b) noexcept {
    a.v[0][0] += b.v[0][0]; a.v[0][1] += b.v[0][1]; a.v[0][2] += b.v[0][2]; a.v[0][3] += b.v[0][3];
    a.v[1][0] += b.v[1][0]; a.v[1][1] += b.v[1][1]; a.v[1][2] += b.v[1][2]; a.v[1][3] += b.v[1][3];
    a.v[2][0] += b.v[2][0]; a.v[2][1] += b.v[2][1]; a.v[2][2] += b.v[2][2]; a.v[2][3] += b.v[2][3];
    a.v[3][0] += b.v[3][0]; a.v[3][1] += b.v[3][1]; a.v[3][2] += b.v[3][2]; a.v[3][3] += b.v[3][3];
    return a;
}

static
Mat4x4F32 operator+(Mat4x4F32 a, const Mat4x4F32& b) noexcept {
    return a += b;
}

static
Mat4x4F32& operator-=(Mat4x4F32& a, const Mat4x4F32& b) noexcept {
    a.v[0][0] -= b.v[0][0]; a.v[0][1] -= b.v[0][1]; a.v[0][2] -= b.v[0][2]; a.v[0][3] -= b.v[0][3];
    a.v[1][0] -= b.v[1][0]; a.v[1][1] -= b.v[1][1]; a.v[1][2] -= b.v[1][2]; a.v[1][3] -= b.v[1][3];
    a.v[2][0] -= b.v[2][0]; a.v[2][1] -= b.v[2][1]; a.v[2][2] -= b.v[2][2]; a.v[2][3] -= b.v[2][3];
    a.v[3][0] -= b.v[3][0]; a.v[3][1] -= b.v[3][1]; a.v[3][2] -= b.v[3][2]; a.v[3][3] -= b.v[3][3];
    return a;
}

static
Mat4x4F32 operator-(Mat4x4F32 a, const Mat4x4F32& b) noexcept {
    return a -= b;
}

static
Mat4x4F32& operator*=(Mat4x4F32& a, const Mat4x4F32& b) noexcept {
    Mat4x4F32 res{};
    res.v[0][0] = a.v[0][0] * b.v[0][0] + a.v[0][1] * b.v[1][0] + a.v[0][2] * b.v[2][0] + a.v[0][3] * b.v[3][0];
    res.v[0][1] = a.v[0][0] * b.v[0][1] + a.v[0][1] * b.v[1][1] + a.v[0][2] * b.v[2][1] + a.v[0][3] * b.v[3][1];
    res.v[0][2] = a.v[0][0] * b.v[0][2] + a.v[0][1] * b.v[1][2] + a.v[0][2] * b.v[2][2] + a.v[0][3] * b.v[3][2];
    res.v[0][3] = a.v[0][0] * b.v[0][3] + a.v[0][1] * b.v[1][3] + a.v[0][2] * b.v[2][3] + a.v[0][3] * b.v[3][3];
    res.v[1][0] = a.v[1][0] * b.v[0][0] + a.v[1][1] * b.v[1][0] + a.v[1][2] * b.v[2][0] + a.v[1][3] * b.v[3][0];
    res.v[1][1] = a.v[1][0] * b.v[0][1] + a.v[1][1] * b.v[1][1] + a.v[1][2] * b.v[2][1] + a.v[1][3] * b.v[3][1];
    res.v[1][2] = a.v[1][0] * b.v[0][2] + a.v[1][1] * b.v[1][2] + a.v[1][2] * b.v[2][2] + a.v[1][3] * b.v[3][2];
    res.v[1][3] = a.v[1][0] * b.v[0][3] + a.v[1][1] * b.v[1][3] + a.v[1][2] * b.v[2][3] + a.v[1][3] * b.v[3][3];
    res.v[2][0] = a.v[2][0] * b.v[0][0] + a.v[2][1] * b.v[1][0] + a.v[2][2] * b.v[2][0] + a.v[2][3] * b.v[3][0];
    res.v[2][1] = a.v[2][0] * b.v[0][1] + a.v[2][1] * b.v[1][1] + a.v[2][2] * b.v[2][1] + a.v[2][3] * b.v[3][1];
    res.v[2][2] = a.v[2][0] * b.v[0][2] + a.v[2][1] * b.v[1][2] + a.v[2][2] * b.v[2][2] + a.v[2][3] * b.v[3][2];
    res.v[2][3] = a.v[2][0] * b.v[0][3] + a.v[2][1] * b.v[1][3] + a.v[2][2] * b.v[2][3] + a.v[2][3] * b.v[3][3];
    res.v[3][0] = a.v[3][0] * b.v[0][0] + a.v[3][1] * b.v[1][0] + a.v[3][2] * b.v[2][0] + a.v[3][3] * b.v[3][0];
    res.v[3][1] = a.v[3][0] * b.v[0][1] + a.v[3][1] * b.v[1][1] + a.v[3][2] * b.v[2][1] + a.v[3][3] * b.v[3][1];
    res.v[3][2] = a.v[3][0] * b.v[0][2] + a.v[3][1] * b.v[1][2] + a.v[3][2] * b.v[2][2] + a.v[3][3] * b.v[3][2];
    res.v[3][3] = a.v[3][0] * b.v[0][3] + a.v[3][1] * b.v[1][3] + a.v[3][2] * b.v[2][3] + a.v[3][3] * b.v[3][3];
    a = res;
    return a;
}

static
Mat4x4F32& operator*=(Mat4x4F32& m, F32 s) noexcept {
    m.v[0][0] *= s; m.v[0][1] *= s; m.v[0][2] *= s; m.v[0][3] *= s;
    m.v[1][0] *= s; m.v[1][1] *= s; m.v[1][2] *= s; m.v[1][3] *= s;
    m.v[2][0] *= s; m.v[2][1] *= s; m.v[2][2] *= s; m.v[2][3] *= s;
    m.v[3][0] *= s; m.v[3][1] *= s; m.v[3][2] *= s; m.v[3][3] *= s;
    return m;
}

static
Mat4x4F32 operator*(Mat4x4F32 m, F32 s) noexcept {
    return m *= s;
}

static
Mat4x4F32 operator*(F32 s, Mat4x4F32 m) noexcept {
    return m * s;
}

static
Mat4x4F32& operator/=(Mat4x4F32& m, F32 s) noexcept {
    F32 inv = 1.0f / s;
    m.v[0][0] *= inv; m.v[0][1] *= inv; m.v[0][2] *= inv; m.v[0][3] *= inv;
    m.v[1][0] *= inv; m.v[1][1] *= inv; m.v[1][2] *= inv; m.v[1][3] *= inv;
    m.v[2][0] *= inv; m.v[2][1] *= inv; m.v[2][2] *= inv; m.v[2][3] *= inv;
    m.v[3][0] *= inv; m.v[3][1] *= inv; m.v[3][2] *= inv; m.v[3][3] *= inv;
    return m;
}

static
Mat4x4F32 operator/(Mat4x4F32 m, F32 s) noexcept {
    return m /= s;
}

static
Vec4F32 operator*(const Vec4F32& v, const Mat4x4F32& m) noexcept {
    Vec4F32 res;
    res.x = v.x * m.v[0][0] + v.y * m.v[1][0] + v.z * m.v[2][0] + v.w * m.v[3][0];
    res.y = v.x * m.v[0][1] + v.y * m.v[1][1] + v.z * m.v[2][1] + v.w * m.v[3][1];
    res.z = v.x * m.v[0][2] + v.y * m.v[1][2] + v.z * m.v[2][2] + v.w * m.v[3][2];
    res.w = v.x * m.v[0][3] + v.y * m.v[1][3] + v.z * m.v[2][3] + v.w * m.v[3][3];
    return res;
}
