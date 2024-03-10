#include "json.hpp"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

// -----------------------------------------------------------------------
// Sprite
Rectangle rect_from_json(json data) {
    Rectangle rect = {
        .x = data["x"], .y = data["y"], .width = data["w"], .height = data["h"]};

    return rect;
}

class Sprite {
  public:
    Texture2D texture;
    Rectangle src;
    std::unordered_map<std::string, Rectangle> masks;

    Sprite(
        Texture2D texture, Rectangle src, std::unordered_map<std::string, Rectangle> masks
    )
        : texture(texture), src(src), masks(masks) {}

    Sprite(json frame_json, Texture2D texture) : texture(texture) {
        json sprite_json = frame_json["sprite"];
        json masks_json = frame_json["masks"];
        this->src = rect_from_json(sprite_json);

        std::unordered_map<std::string, Rectangle> masks;
        for (auto it = masks_json.begin(); it != masks_json.end(); ++it) {
            const auto &mask_name = it.key();
            const auto &mask_json = it.value();
            this->masks[mask_name] = rect_from_json(mask_json);
        }
    }
};

class SpriteSheet {
  private:
    Texture2D texture;
    json meta;

  public:
    SpriteSheet(){};

    SpriteSheet(std::string dir_path, std::string name) {
        std::string meta_file_path = fs::path(dir_path) / fs::path(name + ".json");
        std::string texture_file_path = fs::path(dir_path) / fs::path(name + ".png");

        std::ifstream file(meta_file_path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + meta_file_path);
        }

        meta = json::parse(file);
        file.close();

        texture = LoadTexture(texture_file_path.c_str());
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    }

    int count_frames(std::string &name) {
        return meta["frames"][name].size();
    }

    Sprite get_sprite(std::string &name, int idx) {
        json frame_json = meta["frames"][name][idx];
        Sprite sprite(frame_json, this->texture);
        return sprite;
    }

    void unload() {
        UnloadTexture(texture);
    }
};

class SpriteSheetAnimator {
  private:
    SpriteSheet &sprite_sheet;
    std::string name = "";
    float frame_duration = 0.0;
    float progress = 0.0;
    bool is_repeat = true;

  public:
    SpriteSheetAnimator(SpriteSheet &sprite_sheet) : sprite_sheet(sprite_sheet) {}

    void play(std::string name, float frame_duration, bool is_repeat) {
        this->frame_duration = frame_duration;
        this->is_repeat = is_repeat;

        if (this->name != name) {
            this->name = name;
            this->progress = 0.0;
        }
    }

    void update(float dt) {
        if (this->name.empty()) return;

        int n_frames = this->sprite_sheet.count_frames(this->name);

        this->progress += dt / (n_frames * this->frame_duration);
        if (this->is_repeat) this->progress -= std::floor(this->progress);
        else this->progress = std::fmin(this->progress, 1.0);
    }

    Sprite get_sprite() {
        if (this->name.empty()) {
            throw std::runtime_error(
                "Failed to get the sprite, the animation is not playing"
            );
        }

        int n_frames = this->sprite_sheet.count_frames(this->name);
        int idx = std::round(this->progress * (n_frames - 1.0));
        Sprite sprite = this->sprite_sheet.get_sprite(this->name, idx);
        return sprite;
    }
};

// -----------------------------------------------------------------------
// Resources
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

class Resources {
  public:
    std::unordered_map<std::string, SpriteSheet> sprite_sheets;

    Resources() {
        sprite_sheets["0"] = SpriteSheet("./resources/sprite_sheets/", "0");
    }

    ~Resources() {
        for (auto &pair : sprite_sheets)
            pair.second.unload();
    }

    SpriteSheetAnimator get_sprite_sheet_animator(std::string sprite_sheet_name) {
        SpriteSheetAnimator animator(sprite_sheets[sprite_sheet_name]);
        return animator;
    }
};

// -----------------------------------------------------------------------
// Game camera
class GameCamera {
  private:
    float zoom = 4.0;

  public:
    Camera2D camera2d;

    GameCamera() {
        camera2d = {
            .offset = {0.5 * SCREEN_WIDTH, 0.5 * SCREEN_HEIGHT},
            .target = {0.0, 0.0},
            .rotation = 0.0,
            .zoom = this->zoom};
    }
};

// -----------------------------------------------------------------------
// Knight
enum class KnightState { IDLE };

class Knight {
  public:
    Vector2 position;
    KnightState state;
    SpriteSheetAnimator animator;

    Knight(Resources &resources, Vector2 position)
        : position(position), animator(resources.get_sprite_sheet_animator("0")) {}
};

// -----------------------------------------------------------------------
// World
class World {
  public:
    Knight player;
    GameCamera camera;

    World(Resources &resources) : player(resources, {0.0, 0.0}) {}

    void update() {
        float dt = GetFrameTime();

        // ---------------------------------------------------------------
        // update player
        this->player.animator.play("knight_attack_1", 0.1, true);
        this->player.animator.update(dt);
    }
};

// -----------------------------------------------------------------------
// Renderer
class Renderer {
  private:
    std::unordered_map<std::string, Shader> shaders;

  public:
    Renderer() {
        SetConfigFlags(FLAG_MSAA_4X_HINT);
        SetTargetFPS(60);
        InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Game");

        shaders["sprite"] = load_shader("base.vert", "sprite.frag");
    }

    ~Renderer() {
        for (auto &pair : shaders)
            UnloadShader(pair.second);

        CloseWindow();
    }

    void draw_sprite(Sprite sprite, Vector2 position) {
        Rectangle dst = sprite.src;
        dst.x = position.x - 0.5 * dst.width;
        dst.y = position.y - dst.height;

        BeginShaderMode(this->shaders["sprite"]);
        DrawTexturePro(sprite.texture, sprite.src, dst, Vector2Zero(), 0.0, WHITE);
        EndShaderMode();
    }

    void draw_world(World &world) {
        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode2D(world.camera.camera2d);
        draw_sprite(world.player.animator.get_sprite(), world.player.position);
        EndMode2D();

        EndDrawing();
    }
};

// -----------------------------------------------------------------------
// Main
int main() {
    Renderer renderer;
    Resources resources;
    World world(resources);

    while (!WindowShouldClose()) {
        world.update();
        renderer.draw_world(world);
    }
}
