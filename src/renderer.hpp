#pragma once

#include "field.hpp"
#include "world.hpp"

class Renderer {
private:
    static constexpr int SCREEN_WIDTH = 1024;
    static constexpr int SCREEN_HEIGHT = 768;

    Mesh unit_cube;
    Material default_material;

    void draw_field(Field *field);

public:
    Renderer();
    void draw_world(World *world);
    ~Renderer();
};

