#pragma once

#include "raylib.h"

class MyCamera {
public:
    Camera3D cam;

    MyCamera();
    MyCamera(Vector3 position, Vector3 target, float fov);

    void update_orbital();
};

