#pragma once

#include "field.hpp"
#include "world.hpp"

class Renderer {
private:
    static constexpr int SCREEN_WIDTH = 1024;
    static constexpr int SCREEN_HEIGHT = 768;

    Shader vox_plane_shader;
    unsigned int vox_vao;
    unsigned int vox_cube_center_position_vbo;

    void draw_field(Field *field, Matrix vp);

public:
    Renderer();
    void draw_world(World *world);
    ~Renderer();
};

