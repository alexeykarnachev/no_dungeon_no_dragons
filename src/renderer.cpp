#include "renderer.hpp"

#include "raylib.h"
#include "raymath.h"
#include "rcamera.h"
#include "resources.hpp"
#include "rlgl.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

std::string load_shader_src();
Shader load_shader(const std::string &vs_file_name, const std::string &fs_file_name);

Renderer::Renderer() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    SetTargetFPS(60);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Game");
}

void Renderer::draw_world(World *world, Resources *resources) {
    BeginDrawing();
    ClearBackground(DARKGRAY);

    BeginMode3D(world->camera.cam);
    DrawModel(resources->zed_1_model.model, Vector3Zero(), 0.01, WHITE);
    EndMode3D();

    DrawFPS(0, 0);

    EndDrawing();
}

Renderer::~Renderer() {
    CloseWindow();
}

std::string load_shader_src(const std::string &file_name) {
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

Shader load_shader(const std::string &vs_file_name, const std::string &fs_file_name) {
    std::string vs, fs;

    vs = load_shader_src(vs_file_name);
    fs = load_shader_src(fs_file_name);
    Shader shader = LoadShaderFromMemory(vs.c_str(), fs.c_str());
    return shader;
}
