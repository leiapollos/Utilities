//
// Created by André Leite on 30/12/2025.
//

#pragma once

struct Camera {
    Vec3F32 position;
    Vec3F32 velocity;
    QuatF32 orientation;
    F32 sensitivity;
    F32 moveSpeed;
};

void camera_init(Camera* camera);
void camera_rotate(Camera* camera, F32 deltaPitch, F32 deltaYaw);
Mat4x4F32 camera_get_rotation_matrix(Camera* camera);
Mat4x4F32 camera_get_view_matrix(Camera* camera);
void camera_update(Camera* camera, F32 deltaSeconds);
