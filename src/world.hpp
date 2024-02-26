#pragma once

#include "field.hpp"
#include "camera.hpp"

class World {
public:
    Field field;
    MyCamera camera;

    World();
};
