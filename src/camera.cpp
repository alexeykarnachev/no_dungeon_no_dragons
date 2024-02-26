#include "camera.hpp"

MyCamera::MyCamera() : MyCamera({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f}, 70.0f) {}

MyCamera::MyCamera(Vector3 position, Vector3 target, float fov) {
    cam = {
        .position = position,
        .target = target,
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = fov,
        .projection = CAMERA_PERSPECTIVE
    };
}
