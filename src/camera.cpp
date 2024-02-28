#include "camera.hpp"
#include "raymath.h"
#include "rcamera.h"
#include "raylib.h"

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

void MyCamera::update_orbital() {
    static float rot_speed = 0.003f;
    static float move_speed = 0.01f;
    static float zoom_speed = 1.0f;

    bool is_mmb_down = IsMouseButtonDown(MOUSE_MIDDLE_BUTTON);
    bool is_shift_down = IsKeyDown(KEY_LEFT_SHIFT);
    float mouse_wheel_move = GetMouseWheelMove();
    Vector2 mouse_delta = GetMouseDelta();

    if (is_mmb_down && is_shift_down) {
        // Shift + MMB + mouse move -> change the camera position in the
        // right-direction plane
        CameraMoveRight(&cam, -move_speed * mouse_delta.x, true);

        Vector3 right = GetCameraRight(&cam);
        Vector3 up = Vector3CrossProduct(
            Vector3Subtract(cam.position, cam.target), right
        );
        up = Vector3Scale(Vector3Normalize(up), move_speed * mouse_delta.y);
        cam.position = Vector3Add(cam.position, up);
        cam.target = Vector3Add(cam.target, up);
    } else if (is_mmb_down) {
        // Rotate the camera around the look-at point
        CameraYaw(&cam, -rot_speed * mouse_delta.x, true);
        CameraPitch(&cam, rot_speed * mouse_delta.y, true, true, false);
    }

    // Bring camera closer (or move away), to the look-at point
    CameraMoveToTarget(&cam, -mouse_wheel_move * zoom_speed);
}
