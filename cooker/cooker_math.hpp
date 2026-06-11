//
// Created by André Leite on 11/06/2026.
//
// Flat F32[16] transform helpers for the cooker. The cooker compiles
// against the meta/ tool snapshot (no base_math), so these mirror the
// live base_math conventions exactly: storage flattens v[row][col] as
// row*4+col, points transform as v·M with translation in storage row 3,
// composition applies the LEFT operand first. glTF's column-major flat
// arrays land in this layout byte-for-byte (glTF M·v == our v·M^T and
// the flat bytes coincide). The U11 suite cross-validates these against
// base_math — change both or neither.
//

#pragma once

static void asset_mat_identity(F32* m) {
    for (U32 at = 0u; at < 16u; ++at) {
        m[at] = 0.0f;
    }
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

static void asset_mat_mul(const F32* a, const F32* b, F32* out) {
    F32 result[16];
    for (U32 row = 0u; row < 4u; ++row) {
        for (U32 col = 0u; col < 4u; ++col) {
            result[row * 4u + col] = a[row * 4u + 0u] * b[0u * 4u + col] +
                                     a[row * 4u + 1u] * b[1u * 4u + col] +
                                     a[row * 4u + 2u] * b[2u * 4u + col] +
                                     a[row * 4u + 3u] * b[3u * 4u + col];
        }
    }
    for (U32 at = 0u; at < 16u; ++at) {
        out[at] = result[at];
    }
}

// glTF TRS (translation, rotation quaternion xyzw, scale) composed for the
// row-vector convention: scale, then rotate, then translate = S * R * T.
static void asset_mat_from_trs(const F32* t, const F32* q, const F32* s, F32* out) {
    F32 xx = q[0] * q[0];
    F32 yy = q[1] * q[1];
    F32 zz = q[2] * q[2];
    F32 xy = q[0] * q[1];
    F32 xz = q[0] * q[2];
    F32 yz = q[1] * q[2];
    F32 wx = q[3] * q[0];
    F32 wy = q[3] * q[1];
    F32 wz = q[3] * q[2];

    F32 rotation[16];
    asset_mat_identity(rotation);
    rotation[0] = 1.0f - 2.0f * (yy + zz);
    rotation[1] = 2.0f * (xy + wz);
    rotation[2] = 2.0f * (xz - wy);
    rotation[4] = 2.0f * (xy - wz);
    rotation[5] = 1.0f - 2.0f * (xx + zz);
    rotation[6] = 2.0f * (yz + wx);
    rotation[8] = 2.0f * (xz + wy);
    rotation[9] = 2.0f * (yz - wx);
    rotation[10] = 1.0f - 2.0f * (xx + yy);

    // Scale rows of the rotation (S * R), then translation in storage row 3.
    for (U32 row = 0u; row < 3u; ++row) {
        for (U32 col = 0u; col < 3u; ++col) {
            out[row * 4u + col] = rotation[row * 4u + col] * s[row];
        }
        out[row * 4u + 3u] = 0.0f;
    }
    out[12] = t[0];
    out[13] = t[1];
    out[14] = t[2];
    out[15] = 1.0f;
}

static void asset_mat_transform_point(const F32* m, const F32* p, F32* out) {
    F32 result[3];
    for (U32 col = 0u; col < 3u; ++col) {
        result[col] = p[0] * m[0u * 4u + col] +
                      p[1] * m[1u * 4u + col] +
                      p[2] * m[2u * 4u + col] +
                      m[3u * 4u + col];
    }
    out[0] = result[0];
    out[1] = result[1];
    out[2] = result[2];
}
