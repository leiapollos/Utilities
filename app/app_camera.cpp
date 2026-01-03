//
// Created by André Leite on 30/12/2025.
//

void camera_init(Camera* camera) {
    camera->orientation = quat_identity();
}

void camera_rotate(Camera* camera, F32 deltaPitch, F32 deltaYaw) {
    QuatF32 yawQuat   = quat_from_axis_angle({{0.0f, -1.0f, 0.0f}}, deltaYaw);
    QuatF32 pitchQuat = quat_from_axis_angle({{1.0f, 0.0f, 0.0f}}, deltaPitch);
    
    camera->orientation = quat_mul(yawQuat, camera->orientation);
    camera->orientation = quat_mul(camera->orientation, pitchQuat);
    camera->orientation = quat_normalize(camera->orientation);
}

Mat4x4F32 camera_get_rotation_matrix(Camera* camera) {
    return quat_to_mat4(camera->orientation);
}

Mat4x4F32 camera_get_view_matrix(Camera* camera) {
    QuatF32 invOrientation = quat_conjugate(camera->orientation);
    Mat4x4F32 rotation = quat_to_mat4(invOrientation);
    
    Mat4x4F32 translation = mat4_identity();
    translation.v[3][0] = -camera->position.x;
    translation.v[3][1] = -camera->position.y;
    translation.v[3][2] = -camera->position.z;
    
    return translation * rotation;
}

void camera_update(Camera* camera, F32 deltaSeconds) {
    Mat4x4F32 rotation = camera_get_rotation_matrix(camera);
    
    F32 vx = camera->velocity.x * camera->moveSpeed * deltaSeconds;
    F32 vy = camera->velocity.y * camera->moveSpeed * deltaSeconds;
    F32 vz = camera->velocity.z * camera->moveSpeed * deltaSeconds;
    
    camera->position.x += rotation.v[0][0] * vx + rotation.v[1][0] * vy + rotation.v[2][0] * vz;
    camera->position.y += rotation.v[0][1] * vx + rotation.v[1][1] * vy + rotation.v[2][1] * vz;
    camera->position.z += rotation.v[0][2] * vx + rotation.v[1][2] * vy + rotation.v[2][2] * vz;
}
