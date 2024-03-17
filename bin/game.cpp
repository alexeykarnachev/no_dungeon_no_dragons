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
// utils
json load_json(std::string file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + file_path);
    }

    auto data = json::parse(file);
    file.close();
    return data;
}

// -----------------------------------------------------------------------
// collisions
Vector2 get_aabb_mtv(Rectangle r1, Rectangle r2) {
    Vector2 mtv = Vector2Zero();
    if (!CheckCollisionRecs(r1, r2)) return mtv;

    float x_west = r2.x - r1.x - r1.width;
    float x_east = r2.x + r2.width - r1.x;
    if (fabs(x_west) < fabs(x_east)) mtv.x = x_west;
    else mtv.x = x_east;

    float y_south = r2.y + r2.height - r1.y;
    float y_north = r2.y - r1.y - r1.height;
    if (fabs(y_south) < fabs(y_north)) mtv.y = y_south;
    else mtv.y = y_north;

    if (std::fabs(mtv.x) > std::fabs(mtv.y)) mtv.x = 0.0;
    else mtv.y = 0.0;

    return mtv;
}

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
        meta = load_json(meta_file_path);

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
// tile
class TileSheet {
  private:
    Texture2D texture;
    json meta;

  public:
    TileSheet(){};

    TileSheet(std::string meta_file_path) {
        meta = load_json(meta_file_path);

        std::string texture_file_path = "./resources/tiled/" + std::string(meta["image"]);
        texture = LoadTexture(texture_file_path.c_str());
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);

        // {"columns":32,"image":"tile_sheets/pixel_castle_2d/walls.png","imageheight":512,"imagewidth":512,"margin":0,"name":"walls","spacing":0,"tilecount":1024,"tiledversion":"1.10.2","tileheight":16,"tilewidth":16,"type":"tileset","version":"1.10"}
    }

    // Sprite get_sprite(std::string &name, int idx, Vector2 position, bool is_hflip) {
    //     json frame_json = meta["frames"][name][idx];
    //     Sprite sprite(frame_json, this->texture, position, is_hflip);
    //     return sprite;
    // }

    void unload() {
        UnloadTexture(texture);
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
    std::unordered_map<std::string, json> level_jsons;
    std::unordered_map<std::string, TileSheet> tile_sheets;

    Resources() {
        sprite_sheets["0"] = SpriteSheet("./resources/sprite_sheets/", "0");

        level_jsons["0"] = load_json("./resources/tiled/level_0.json");
        for (auto [_, level_json] : level_jsons) {
            for (auto tileset_json : level_json["tilesets"]) {
                std::string name = tileset_json["source"];
                std::string meta_file_path = "./resources/tiled/" + std::string(name);
                auto tile_sheet = TileSheet(meta_file_path);
                if (!HASHMAP_GET_OR_NULL(tile_sheets, name)) {
                    tile_sheets[name] = tile_sheet;
                }
            }
        }
    }

    ~Resources() {
        for (auto &pair : sprite_sheets)
            pair.second.unload();
        for (auto &pair : tile_sheets)
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
    float zoom = 3.0;

  public:
    Camera2D camera2d;

    GameCamera() {
        camera2d = {
            .offset = {0.5 * SCREEN_WIDTH, 0.5 * SCREEN_HEIGHT + 200.0},
            .target = {0.0, 0.0},
            .rotation = 0.0,
            .zoom = this->zoom};
    }
};

// -----------------------------------------------------------------------
// creatures

enum class CreatureState {
    IDLE,
    MOVING,
};

enum class CreatureType {
    PLAYER,
};

class Creature {
  public:
    CreatureType type;
    CreatureState state;
    SpriteSheetAnimator animator;

    Vector2 position = {0.0, 0.0};
    Vector2 velocity = {0.0, 0.0};

    bool is_hflip = false;
    bool is_grounded = false;
    bool has_weight = false;

    Creature(
        CreatureType type,
        CreatureState state,
        SpriteSheetAnimator animator,
        Vector2 position,
        bool has_weight
    )
        : type(type)
        , state(state)
        , position(position)
        , animator(animator)
        , has_weight(has_weight) {}

    Rectangle *get_rigid_collider() {
        return this->animator.get_sprite(position, is_hflip).get_mask("rigid");
    }
};

// -----------------------------------------------------------------------
// world
class World {
  public:
    std::vector<Creature> creatures;
    std::vector<Rectangle> colliders;

    GameCamera camera;
    float gravity = 400.0;

    World(Resources &resources) {
        auto level_json = resources.level_jsons["0"];
        for (auto layer_json : level_json["layers"]) {
            std::string layer_name = std::string(layer_json["name"]);
            for (auto object : layer_json["objects"]) {
                for (auto property : object["properties"]) {

                    auto property_name = property["name"];
                    auto property_value = property["value"];
                    auto object_x = object["x"];
                    auto object_y = object["y"];
                    auto object_width = object["width"];
                    auto object_height = object["height"];

                    if (layer_name == "colliders") {
                        if (property_name == "is_rigid" && property_value) {
                            this->colliders.push_back(
                                {.x = object_x,
                                 .y = object_y,
                                 .width = object_width,
                                 .height = object_height}
                            );
                        }
                    } else if (layer_name == "creatures") {
                        if (property_name == "type" && property_value == "player") {
                            Creature player(
                                CreatureType::PLAYER,
                                CreatureState::IDLE,
                                resources.get_sprite_sheet_animator("0"),
                                {.x = object_x, .y = object_y},
                                true
                            );
                            this->creatures.push_back(player);
                            this->camera.camera2d.target = player.position;
                        }
                    }
                }
            }
        }
    }

    void update() {
        float dt = GetFrameTime();

        for (auto &creature : this->creatures) {
            if (creature.has_weight) {
                creature.velocity.y += dt * this->gravity;
            }

            Vector2 step = Vector2Scale(creature.velocity, dt);
            if (creature.type == CreatureType::PLAYER) {
                creature.state = CreatureState::IDLE;
                if (IsKeyDown(KEY_A)) {
                    step.x -= 90.0 * dt;
                    creature.is_hflip = true;
                    creature.state = CreatureState::MOVING;
                }
                if (IsKeyDown(KEY_D)) {
                    step.x += 90.0 * dt;
                    creature.is_hflip = false;
                    creature.state = CreatureState::MOVING;
                }
                if (IsKeyDown(KEY_W)) {
                    step.y -= 90.0 * dt;
                    creature.state = CreatureState::MOVING;
                }
                if (IsKeyDown(KEY_S)) {
                    step.y += 90.0 * dt;
                    creature.state = CreatureState::MOVING;
                }

                if (creature.state == CreatureState::MOVING) {
                    creature.animator.play("knight_run", 0.1, true);
                } else if (creature.state == CreatureState::IDLE) {
                    creature.animator.play("knight_idle", 0.1, true);
                }
            }

            creature.position = Vector2Add(creature.position, step);

            Rectangle *my_collider = creature.get_rigid_collider();
            if (my_collider) {
                creature.is_grounded = false;
                for (auto &collider : this->colliders) {
                    Vector2 mtv = get_aabb_mtv(*my_collider, collider);
                    creature.position = Vector2Add(creature.position, mtv);
                    if (fabs(mtv.y) > EPSILON) creature.velocity.y = 0.0;
                    if (fabs(mtv.x) > EPSILON) creature.velocity.x = 0.0;
                    creature.is_grounded |= mtv.y > EPSILON;
                }
            }

            creature.animator.update(dt);
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
        for (auto &creature : world.creatures) {
            Sprite sprite = creature.animator.get_sprite(
                creature.position, creature.is_hflip
            );
            sprite.draw();
        }
        EndShaderMode();

        // ---------------------------------------------------------------
        // draw masks
        for (auto &creature : world.creatures) {
            Sprite sprite = creature.animator.get_sprite(
                creature.position, creature.is_hflip
            );
            Rectangle *mask = sprite.get_mask("rigid");
            if (mask) {
                DrawRectangleRec(*mask, ColorAlpha(GREEN, 0.2));
            }
        }

        // ---------------------------------------------------------------
        // draw colliders
        for (auto &collider : world.colliders) {
            DrawRectangleRec(collider, ColorAlpha(RED, 0.2));
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
