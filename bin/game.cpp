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

#define HASHMAP_GET_OR_NULL(map, key) \
    ((map).find(key) != (map).end() ? &((map)[key]) : nullptr)

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

// -----------------------------------------------------------------------
// sprite
Rectangle rect_from_json(json data) {
    Rectangle rect = {
        .x = data["x"], .y = data["y"], .width = data["w"], .height = data["h"]};

    return rect;
}

class Sprite {
  private:
    Texture2D texture;
    Rectangle src;
    Rectangle dst;
    std::unordered_map<std::string, Rectangle> masks;

  public:
    Sprite(
        Texture2D texture,
        Rectangle src,
        Rectangle dst,
        std::unordered_map<std::string, Rectangle> masks
    )
        : texture(texture)
        , src(src)
        , dst(dst)
        , masks(masks) {}

    Sprite(json frame_json, Texture2D texture, Vector2 position, bool is_hflip)
        : texture(texture) {
        json sprite_json = frame_json["sprite"];
        json masks_json = frame_json["masks"];

        Rectangle src = rect_from_json(sprite_json);
        Rectangle dst = src;
        dst.x = position.x - 0.5 * dst.width;
        dst.y = position.y - dst.height;

        std::unordered_map<std::string, Rectangle> masks;
        for (auto it = masks_json.begin(); it != masks_json.end(); ++it) {
            const auto &mask_name = it.key();
            const auto &mask_json = it.value();

            Rectangle mask = rect_from_json(mask_json);
            Vector2 origin = {position.x - 0.5f * src.width, position.y - src.height};
            mask.y = origin.y + mask.y;
            mask.x = is_hflip ? origin.x - mask.x + src.width - mask.width
                              : origin.x + mask.x;
            this->masks[mask_name] = mask;
        }

        src.width = is_hflip ? -src.width : src.width;

        this->src = src;
        this->dst = dst;
    }

    void draw() {
        DrawTexturePro(this->texture, this->src, this->dst, Vector2Zero(), 0.0, WHITE);
    }

    Rectangle *get_mask(std::string name) {
        return HASHMAP_GET_OR_NULL(this->masks, name);
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

    Sprite get_sprite(std::string &name, int idx, Vector2 position, bool is_hflip) {
        json frame_json = meta["frames"][name][idx];
        Sprite sprite(frame_json, this->texture, position, is_hflip);
        return sprite;
    }

    void unload() {
        UnloadTexture(texture);
    }
};

class SpriteSheetAnimator {
  private:
    SpriteSheet *sprite_sheet;
    std::string name = "";
    float frame_duration = 0.0;
    float progress = 0.0;
    bool is_repeat = true;

  public:
    SpriteSheetAnimator() {}
    SpriteSheetAnimator(SpriteSheet *sprite_sheet)
        : sprite_sheet(sprite_sheet) {}

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

        int n_frames = this->sprite_sheet->count_frames(this->name);

        this->progress += dt / (n_frames * this->frame_duration);
        if (this->is_repeat) this->progress -= std::floor(this->progress);
        else this->progress = std::fmin(this->progress, 1.0);
    }

    Sprite get_sprite(Vector2 position, bool is_hflip) {
        if (this->name.empty()) {
            throw std::runtime_error(
                "Failed to get the sprite, the animation is not playing"
            );
        }

        int n_frames = this->sprite_sheet->count_frames(this->name);
        int idx = std::round(this->progress * (n_frames - 1.0));
        Sprite sprite = this->sprite_sheet->get_sprite(
            this->name, idx, position, is_hflip
        );
        return sprite;
    }
};

// -----------------------------------------------------------------------
// resources
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
        SpriteSheetAnimator animator(&sprite_sheets[sprite_sheet_name]);
        return animator;
    }
};

// -----------------------------------------------------------------------
// game camera
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
// object and components

enum class GameObjectState {
    IDLE,
    MOVING,
};

enum class GameObjectType {
    PLAYER,
};

class GameObject {
  public:
    GameObjectType type;
    GameObjectState state;
    SpriteSheetAnimator animator;

    Vector2 position = {0.0, 0.0};
    Vector2 velocity = {0.0, 0.0};

    bool is_hflip = false;
    bool has_weight = false;

    GameObject(
        GameObjectType type,
        GameObjectState state,
        SpriteSheetAnimator animator,
        Vector2 position,
        bool has_weight
    )
        : type(type)
        , state(state)
        , position(position)
        , animator(animator)
        , has_weight(has_weight) {}
};

// -----------------------------------------------------------------------
// world
class World {
  public:
    std::vector<GameObject> game_objects;
    GameCamera camera;
    float gravity = 600.0;

    World(Resources &resources) {
        GameObject player(
            GameObjectType::PLAYER,
            GameObjectState::IDLE,
            resources.get_sprite_sheet_animator("0"),
            Vector2Zero(),
            true
        );

        game_objects.push_back(player);
    }

    void update() {
        float dt = GetFrameTime();

        for (auto &object : this->game_objects) {

            if (object.has_weight) {
                // object.velocity.y += dt * this->gravity;
            }

            Vector2 step = Vector2Scale(object.velocity, dt);

            if (object.type == GameObjectType::PLAYER) {
                if (IsKeyDown(KEY_A)) {
                    step.x -= 90.0 * dt;
                    object.is_hflip = true;
                    object.state = GameObjectState::MOVING;
                } else if (IsKeyDown(KEY_D)) {
                    step.x += 90.0 * dt;
                    object.is_hflip = false;
                    object.state = GameObjectState::MOVING;
                } else {
                    object.state = GameObjectState::IDLE;
                }

                if (object.state == GameObjectState::MOVING) {
                    object.animator.play("knight_run", 0.1, true);
                } else if (object.state == GameObjectState::IDLE) {
                    object.animator.play("knight_idle", 0.1, true);
                }
            }

            object.animator.update(dt);
            object.position = Vector2Add(object.position, step);
        }
    }
};

// -----------------------------------------------------------------------
// renderer
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

    void draw_world(World &world) {
        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode2D(world.camera.camera2d);

        // ---------------------------------------------------------------
        // draw sprites
        BeginShaderMode(this->shaders["sprite"]);
        for (auto &object : world.game_objects) {
            Sprite sprite = object.animator.get_sprite(object.position, object.is_hflip);
            sprite.draw();
        }
        EndShaderMode();

        // ---------------------------------------------------------------
        // draw masks
        for (auto &object : world.game_objects) {
            Sprite sprite = object.animator.get_sprite(object.position, object.is_hflip);
            Rectangle *mask = sprite.get_mask("rigid");
            if (mask) {
                DrawRectangleRec(*mask, ColorAlpha(RED, 0.5));
            }
        }

        EndMode2D();

        EndDrawing();
    }
};

// -----------------------------------------------------------------------
// main loop
int main() {
    Renderer renderer;
    Resources resources;
    World world(resources);

    while (!WindowShouldClose()) {
        world.update();
        renderer.draw_world(world);
    }
}
