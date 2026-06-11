//
// Created by André Leite on 11/06/2026.
//
// Pure CPU world-renderer kernels. The conventions that bit (composition
// order, winding, cull formulas, transparent ordering) live here so the
// module and `./sob test` execute the same code. No gfx, no app state.
//

#pragma once

static Mat4x4F32 app_world_camera_view_proj_(Vec3F32 eye, Vec3F32 target, Vec3F32 up,
                                             F32 fovYRadians, F32 aspect,
                                             F32 zNear, F32 zFar) {
    Mat4x4F32 view = mat4_look_at(eye, target, up);
    Mat4x4F32 projection = mat4_perspective(fovYRadians, aspect, zNear, zFar);
    // Row-vector convention (points transform as v·M, translation in
    // storage row 3): the composite is view * projection; the reverse
    // order degenerates to clipW = 0.
    return view * projection;
}

static void app_world_frustum_planes_(const Mat4x4F32* m, F32* outPlanes) {
    // Gribb-Hartmann from column-major viewProj; rowI[j] = v[j][i].
    for (U32 planeIndex = 0u; planeIndex < 6u; ++planeIndex) {
        F32 plane[4];
        U32 row = planeIndex / 2u;
        B32 add = (planeIndex & 1u) == 0u;
        for (U32 component = 0u; component < 4u; ++component) {
            F32 row3 = m->v[component][3];
            F32 rowN = m->v[component][row];
            plane[component] = add ? (row3 + rowN) : (row3 - rowN);
        }
        if (planeIndex == 4u) {
            // Near plane for [0,1] clip depth is row2 itself.
            for (U32 component = 0u; component < 4u; ++component) {
                plane[component] = m->v[component][2];
            }
        }
        F32 lengthSq = plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2];
        F32 inverseLength = (lengthSq > 0.0f) ? (1.0f / SQRT_F32(lengthSq)) : 0.0f;
        for (U32 component = 0u; component < 4u; ++component) {
            outPlanes[planeIndex * 4u + component] = plane[component] * inverseLength;
        }
    }
}

static B32 app_world_sphere_visible_(const F32* planes, const F32* center, F32 radius) {
    for (U32 plane = 0u; plane < 6u; ++plane) {
        F32 distance = planes[plane * 4u + 0u] * center[0] +
                       planes[plane * 4u + 1u] * center[1] +
                       planes[plane * 4u + 2u] * center[2] +
                       planes[plane * 4u + 3u];
        if (distance < -radius) {
            return 0;
        }
    }
    return 1;
}

static F32 app_world_transparent_depth_(const F32* center, F32 radius,
                                        const F32* eye, Vec3F32 forward) {
    // Nearest-point key: center depth minus radius draws engulfing shells
    // after their contents under the back-to-front order.
    return (center[0] - eye[0]) * forward.x +
           (center[1] - eye[1]) * forward.y +
           (center[2] - eye[2]) * forward.z -
           radius;
}

static void app_world_order_ascending_(const F32* depths, U32* order, U32* scratch, U32 count) {
    for (U32 at = 0u; at < count; ++at) {
        order[at] = at;
    }
    // Stable ascending radix over flipped F32 bits (negatives sort first),
    // two 16-bit passes; the result lands back in the caller's order array.
    for (U32 pass = 0u; pass < 2u; ++pass) {
        U32 shift = pass * 16u;
        U32 histogram[65536];
        MEMSET(histogram, 0, sizeof(histogram));
        for (U32 at = 0u; at < count; ++at) {
            union { F32 f; U32 u; } bits;
            bits.f = depths[order[at]];
            U32 key = ((bits.u >> 31u) != 0u) ? ~bits.u : (bits.u | 0x80000000u);
            histogram[(key >> shift) & 0xFFFFu] += 1u;
        }
        U32 running = 0u;
        for (U32 bucket = 0u; bucket < 65536u; ++bucket) {
            U32 bucketCount = histogram[bucket];
            histogram[bucket] = running;
            running += bucketCount;
        }
        for (U32 at = 0u; at < count; ++at) {
            union { F32 f; U32 u; } bits;
            bits.f = depths[order[at]];
            U32 key = ((bits.u >> 31u) != 0u) ? ~bits.u : (bits.u | 0x80000000u);
            scratch[histogram[(key >> shift) & 0xFFFFu]++] = order[at];
        }
        U32* swap = order;
        order = scratch;
        scratch = swap;
    }
}

struct AppWorldMeshBuilder {
    ShdWorldVertexRecord* vertices;
    U32* indices;
    U32 vertexCount;
    U32 indexCount;
    U32 vertexCapacity;
    U32 indexCapacity;
};

static void app_world_builder_vertex_(AppWorldMeshBuilder* builder, F32 px, F32 py, F32 pz,
                                      F32 nx, F32 ny, F32 nz, F32 u, F32 v) {
    if (builder->vertexCount >= builder->vertexCapacity) {
        return;
    }
    ShdWorldVertexRecord* vertex = builder->vertices + builder->vertexCount;
    builder->vertexCount += 1u;
    vertex->position[0] = px;
    vertex->position[1] = py;
    vertex->position[2] = pz;
    vertex->normal[0] = nx;
    vertex->normal[1] = ny;
    vertex->normal[2] = nz;
    vertex->uv[0] = u;
    vertex->uv[1] = v;
}

static void app_world_builder_index_(AppWorldMeshBuilder* builder, U32 a, U32 b, U32 c) {
    if (builder->indexCount + 3u > builder->indexCapacity) {
        return;
    }
    builder->indices[builder->indexCount + 0u] = a;
    builder->indices[builder->indexCount + 1u] = b;
    builder->indices[builder->indexCount + 2u] = c;
    builder->indexCount += 3u;
}

static void app_world_build_cube_(AppWorldMeshBuilder* builder) {
    static const F32 faces[6][3] = {
        {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
    };
    for (U32 face = 0u; face < 6u; ++face) {
        F32 nx = faces[face][0];
        F32 ny = faces[face][1];
        F32 nz = faces[face][2];
        F32 ux = ny;
        F32 uy = nz;
        F32 uz = nx;
        F32 vx = ny * uz - nz * uy;
        F32 vy = nz * ux - nx * uz;
        F32 vz = nx * uy - ny * ux;
        U32 base = builder->vertexCount;
        for (U32 corner = 0u; corner < 4u; ++corner) {
            F32 s = (corner == 1u || corner == 2u) ? 0.5f : -0.5f;
            F32 t = (corner >= 2u) ? 0.5f : -0.5f;
            app_world_builder_vertex_(builder,
                                      nx * 0.5f + ux * s + vx * t,
                                      ny * 0.5f + uy * s + vy * t,
                                      nz * 0.5f + uz * s + vz * t,
                                      nx, ny, nz,
                                      (corner == 1u || corner == 2u) ? 1.0f : 0.0f,
                                      (corner >= 2u) ? 1.0f : 0.0f);
        }
        app_world_builder_index_(builder, base + 0u, base + 1u, base + 2u);
        app_world_builder_index_(builder, base + 0u, base + 2u, base + 3u);
    }
}

static void app_world_build_sphere_(AppWorldMeshBuilder* builder, U32 rings, U32 sectors) {
    U32 base = builder->vertexCount;
    for (U32 ring = 0u; ring <= rings; ++ring) {
        F32 v = (F32)ring / (F32)rings;
        F32 phi = v * 3.14159265f;
        F32 y = COS_F32(phi);
        F32 r = SIN_F32(phi);
        for (U32 sector = 0u; sector <= sectors; ++sector) {
            F32 u = (F32)sector / (F32)sectors;
            F32 theta = u * 2.0f * 3.14159265f;
            F32 x = r * COS_F32(theta);
            F32 z = r * SIN_F32(theta);
            app_world_builder_vertex_(builder, x * 0.5f, y * 0.5f, z * 0.5f, x, y, z, u, v);
        }
    }
    for (U32 ring = 0u; ring < rings; ++ring) {
        for (U32 sector = 0u; sector < sectors; ++sector) {
            U32 a = base + ring * (sectors + 1u) + sector;
            U32 b = a + sectors + 1u;
            // Wound to match the cube/plane convention (right-handed cross
            // points outward); the transparent pipeline culls on it.
            app_world_builder_index_(builder, a, a + 1u, b);
            app_world_builder_index_(builder, a + 1u, b + 1u, b);
        }
    }
}

static void app_world_build_plane_(AppWorldMeshBuilder* builder) {
    U32 base = builder->vertexCount;
    app_world_builder_vertex_(builder, -0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    app_world_builder_vertex_(builder, 0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f);
    app_world_builder_vertex_(builder, 0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f);
    app_world_builder_vertex_(builder, -0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f);
    app_world_builder_index_(builder, base + 0u, base + 2u, base + 1u);
    app_world_builder_index_(builder, base + 0u, base + 3u, base + 2u);
}
