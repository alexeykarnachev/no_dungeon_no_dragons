#include "renderer.hpp"
#include "raymath.h"
#include "rcamera.h"
#include "raylib.h"
#include "rlgl.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

std::string load_shader_src();
Shader load_shader(const std::string& vs_file_name, const std::string& fs_file_name);

Renderer::Renderer() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    SetTargetFPS(60);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Game");

    vox_plane_shader = load_shader("vox_plane.vert", "vox_plane.frag");

    vox_vao = rlLoadVertexArray();
    rlEnableVertexArray(vox_vao);

    vox_cube_position_vbo = rlLoadVertexBuffer(0, 256 * 3 * 4, true);
    rlSetVertexAttribute(0, 3, RL_FLOAT, false, 0, 0);
    rlEnableVertexAttribute(0);

    vox_face_id_vbo = rlLoadVertexBuffer(0, 256 * 3 * 1, true);
    rlSetVertexAttribute(1, 1, RL_UNSIGNED_BYTE, false, 0, 0);
    rlEnableVertexAttribute(1);
}

void Renderer::draw_field(Field *field) {
    rlEnableVertexArray(vox_vao);
    rlEnableShader(vox_plane_shader.id);

    Matrix view = rlGetMatrixModelview();
    Matrix proj = rlGetMatrixProjection();
    Matrix vp = MatrixMultiply(view, proj);

    int u_vp = GetShaderLocation(vox_plane_shader, "u_vp");
    int u_cube_size = GetShaderLocation(vox_plane_shader, "u_cube_size");

    // rlDisableBackfaceCulling();

#define N 36
    unsigned char face_ids[N] = {
        0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 2, 2,
        3, 3, 3, 3, 3, 3,
        4, 4, 4, 4, 4, 4,
        5, 5, 5, 5, 5, 5
    };
    rlUpdateVertexBuffer(vox_face_id_vbo, face_ids, N, 0); 

    float cube_size = 1.0;
    SetShaderValueMatrix(vox_plane_shader, u_vp, vp);
    SetShaderValue(vox_plane_shader, u_cube_size, &cube_size, SHADER_UNIFORM_FLOAT);

    rlDrawVertexArray(0, N);

    rlDisableShader();
}

void Renderer::draw_world(World *world) {
    BeginDrawing();
    ClearBackground(DARKGRAY);

    BeginMode3D(world->camera.cam);
    draw_field(&world->field);
    EndMode3D();

    DrawFPS(0, 0);

    EndDrawing();
}

Renderer::~Renderer() {
    UnloadShader(vox_plane_shader);
    CloseWindow();
}

std::string load_shader_src(const std::string& file_name) {
    const std::string version_src = "#version 460 core";
    std::ifstream common_file("resources/shaders/common.glsl");
    std::ifstream shader_file("resources/shaders/" + file_name);

    std::stringstream common_stream, shader_stream;
    common_stream << common_file.rdbuf();
    shader_stream << shader_file.rdbuf();

    std::string common_src = common_stream.str();
    std::string shader_src = shader_stream.str();

    std::string full_src = version_src + "\n" + common_src + "\n" + shader_src;

    return full_src;
}

Shader load_shader(const std::string& vs_file_name, const std::string& fs_file_name) {
    std::string vs, fs;

    vs = load_shader_src(vs_file_name);
    fs = load_shader_src(fs_file_name);
    Shader shader = LoadShaderFromMemory(vs.c_str(), fs.c_str());
    return shader;
}
