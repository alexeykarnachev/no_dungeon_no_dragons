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

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080

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
    Sprite() {}

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
    bool is_repeat = true;

  public:
    float progress = 0.0;
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

    bool is_finished() {
        return this->progress == 1.0 && !this->is_repeat;
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
// tiled level
class TileSheet {
  private:
    Texture2D texture;
    json meta;
    int tile_width;
    int tile_height;

  public:
    int n_tiles;
    TileSheet(){};

    TileSheet(std::string meta_file_path) {
        meta = load_json(meta_file_path);

        this->tile_width = meta["tilewidth"];
        this->tile_height = meta["tileheight"];
        this->n_tiles = meta["tilecount"];

        std::string texture_file_path = "./resources/tiled/" + std::string(meta["image"]);
        this->texture = LoadTexture(texture_file_path.c_str());
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    }

    Sprite get_sprite(int idx, Vector2 position) {
        int n_cols = this->texture.width / this->tile_width;
        int n_rows = this->texture.height / this->tile_height;
        int i_row = idx / n_cols;
        int i_col = idx % n_cols;
        float src_x = i_col * this->tile_width;
        float src_y = i_row * this->tile_height;
        float src_width = this->tile_width;
        float src_height = this->tile_height;
        float dst_x = position.x;
        float dst_y = position.y;
        float dst_width = this->tile_width;
        float dst_height = this->tile_height;
        Rectangle src = {
            .x = src_x, .y = src_y, .width = src_width, .height = src_height};
        Rectangle dst = {
            .x = dst_x, .y = dst_y, .width = dst_width, .height = dst_height};
        Sprite sprite(this->texture, src, dst, {});
        return sprite;
    }

    void unload() {
        UnloadTexture(texture);
    }
};

class TiledLevel {
  private:
    std::unordered_map<std::string, TileSheet> tile_sheets;

  public:
    json meta;

    TiledLevel() {}

    TiledLevel(std::string dir_path, std::string name) {
        this->meta = load_json(fs::path(dir_path) / fs::path(name + ".json"));
        for (auto tileset_json : meta["tilesets"]) {
            std::string name = tileset_json["source"];
            std::string meta_file_path = fs::path(dir_path) / std::string(name);
            auto tile_sheet = TileSheet(meta_file_path);
            if (!HASHMAP_GET_OR_NULL(this->tile_sheets, name)) {
                this->tile_sheets[name] = tile_sheet;
            }
        }
    }

    Sprite get_sprite(int tile_id, Vector2 position) {
        for (auto &tileset_json : this->meta["tilesets"]) {
            int tile_first_id = tileset_json["firstgid"];
            if (tile_id < tile_first_id) continue;

            TileSheet tile_sheet = this->tile_sheets[tileset_json["source"]];
            int tile_last_id = tile_sheet.n_tiles + tile_first_id - 1;
            if (tile_id > tile_last_id) continue;

            return tile_sheet.get_sprite(tile_id - tile_first_id, position);
        }

        return Sprite();
    }

    void unload() {
        for (auto &item : this->tile_sheets) {
            item.second.unload();
        }
        this->tile_sheets.clear();
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
    JUMPING,
    FALLING,
    LANDING,
    DASHING,
    ATTACK_0,
};

enum class CreatureType {
    PLAYER,
};

class Creature {
  public:
    CreatureType type;
    CreatureState state;
    SpriteSheetAnimator animator;

    float move_speed;

    Vector2 position = {0.0, 0.0};
    Vector2 velocity = {0.0, 0.0};

    bool is_hflip = false;
    bool is_grounded = false;
    bool has_weight = false;
    float landed_at_speed = 0.0;

    Creature(
        CreatureType type,
        CreatureState state,
        SpriteSheetAnimator animator,
        float move_speed,
        Vector2 position,
        bool has_weight
    )
        : type(type)
        , state(state)
        , position(position)
        , move_speed(move_speed)
        , animator(animator)
        , has_weight(has_weight) {}

    Rectangle *get_rigid_collider() {
        return this->animator.get_sprite(position, is_hflip).get_mask("rigid");
    }
};

// -----------------------------------------------------------------------
// game
class Game {
  public:
    std::unordered_map<std::string, Shader> shaders;
    std::unordered_map<std::string, SpriteSheet> sprite_sheets;
    TiledLevel tiled_level;

    std::vector<Creature> creatures;
    std::vector<Rectangle> colliders;

    GameCamera camera;
    float gravity = 400.0;

    Game() {
        SetConfigFlags(FLAG_MSAA_4X_HINT);
        SetTargetFPS(60);
        InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Game");

        this->shaders["sprite"] = load_shader("base.vert", "sprite.frag");
        this->sprite_sheets["0"] = SpriteSheet("./resources/sprite_sheets/", "0");
        this->load_level("./resources/tiled/", "level_0");
    }

    void load_level(std::string dir_path, std::string name) {
        this->tiled_level.unload();
        this->tiled_level = TiledLevel(dir_path, name);

        for (auto layer_json : this->tiled_level.meta["layers"]) {
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
                                SpriteSheetAnimator(&this->sprite_sheets["0"]),
                                90.0,
                                {.x = object_x, .y = object_y},
                                true
                            );
                            player.animator.play("knight_idle", 0.1, false);
                            this->creatures.push_back(player);
                            this->camera.camera2d.target = player.position;
                        }
                    }
                }
            }
        }
    }

    ~Game() {
        tiled_level.unload();

        for (auto &pair : shaders)
            UnloadShader(pair.second);
        for (auto &pair : this->sprite_sheets)
            pair.second.unload();

        CloseWindow();
    }

    void update() {
        float dt = GetFrameTime();

        this->camera.camera2d.target = this->creatures[0].position;

        for (auto &creature : this->creatures) {
            creature.animator.update(dt);

            // -----------------------------------------------------------
            // immediate velocity and position needs to be computed by
            // the Character update logic
            Vector2 velocity_step = Vector2Zero();
            Vector2 position_step = Vector2Zero();

            // gravity
            if (creature.has_weight) {
                velocity_step.y += dt * this->gravity;
            }

            // -----------------------------------------------------------
            // update player
            if (creature.type == CreatureType::PLAYER) {
                switch (creature.state) {
                    case CreatureState::IDLE:
                        // -> IDLE, MOVING, JUMPING, FALLING, ATTACK_0
                        creature.animator.play("knight_idle", 0.1, true);

                        // move
                        if (IsKeyDown(KEY_D)) position_step.x += creature.move_speed * dt;
                        if (IsKeyDown(KEY_A)) position_step.x -= creature.move_speed * dt;

                        // -> FALLING
                        if (!creature.is_grounded) {
                            creature.state = CreatureState::FALLING;
                            break;
                        }

                        // -> MOVING
                        if (position_step.x) {
                            creature.state = CreatureState::MOVING;
                        }

                        // -> JUMPING
                        if (IsKeyPressed(KEY_W)) {
                            velocity_step.y -= 250.0;
                            creature.state = CreatureState::JUMPING;
                        }

                        break;
                    case CreatureState::MOVING:
                        // -> IDLE, MOVING, JUMPING, FALLING, DASHING, ATTACK_0
                        creature.animator.play("knight_run", 0.1, true);

                        // move
                        if (IsKeyDown(KEY_D)) position_step.x += creature.move_speed * dt;
                        if (IsKeyDown(KEY_A)) position_step.x -= creature.move_speed * dt;

                        // -> IDLE
                        if (!position_step.x) {
                            creature.state = CreatureState::IDLE;
                        }

                        // -> JUMPING
                        if (IsKeyPressed(KEY_W)) {
                            velocity_step.y -= 250.0;
                            creature.state = CreatureState::JUMPING;
                        }

                        // -> FALLING
                        if (!creature.is_grounded) {
                            creature.state = CreatureState::FALLING;
                        }

                        // -> DASHING
                        if (IsKeyPressed(KEY_LEFT_CONTROL)) {
                            float dir = creature.is_hflip ? -1.0 : 1.0;
                            creature.velocity.x = dir * creature.move_speed;
                            creature.state = CreatureState::DASHING;
                        }

                        break;
                    case CreatureState::JUMPING:
                        // -> FALLING
                        creature.animator.play("knight_jump", 0.1, false);

                        // move
                        if (IsKeyDown(KEY_D)) position_step.x += creature.move_speed * dt;
                        if (IsKeyDown(KEY_A)) position_step.x -= creature.move_speed * dt;

                        // -> FALLING
                        if (creature.velocity.y > EPSILON) {
                            creature.state = CreatureState::FALLING;
                        }

                        break;
                    case CreatureState::FALLING:
                        // -> IDLE, MOVING, LANDING
                        creature.animator.play("knight_fall", 0.1, false);

                        // move
                        if (IsKeyDown(KEY_D)) position_step.x += creature.move_speed * dt;
                        if (IsKeyDown(KEY_A)) position_step.x -= creature.move_speed * dt;

                        if (creature.landed_at_speed == 0.0) break;

                        // -> IDLE
                        if (!position_step.x) {
                            creature.state = CreatureState::IDLE;
                        }

                        // -> MOVING
                        if (position_step.x) {
                            creature.state = CreatureState::MOVING;
                        }

                        // -> LANDING
                        if (creature.landed_at_speed > 260.0) {
                            creature.state = CreatureState::LANDING;
                        }

                        break;
                    case CreatureState::LANDING:
                        // -> IDLE, DASHING
                        creature.animator.play("knight_landing", 0.1, false);

                        // -> IDLE
                        if (creature.animator.is_finished()) {
                            creature.state = CreatureState::IDLE;
                        }

                        // -> DASHING
                        if (creature.animator.progress < 0.5
                            && IsKeyPressed(KEY_LEFT_CONTROL)) {
                            float dir = creature.is_hflip ? -1.0 : 1.0;
                            creature.velocity.x = dir * creature.move_speed;
                            creature.state = CreatureState::DASHING;
                        }

                        break;
                    case CreatureState::DASHING:
                        // -> IDLE, FALLING
                        creature.animator.play("knight_roll", 0.1, false);

                        if (!creature.animator.is_finished()) break;
                        creature.velocity.x = 0.0;

                        // -> IDLE
                        if (creature.is_grounded) {
                            creature.state = CreatureState::IDLE;
                        }

                        // -> FALLING
                        if (!creature.is_grounded) {
                            creature.state = CreatureState::FALLING;
                        }

                        break;
                    case CreatureState::ATTACK_0:
                        // -> IDLE
                        break;
                }
            }

            // -----------------------------------------------------------
            // apply immediate velocity and position steps
            creature.velocity = Vector2Add(velocity_step, creature.velocity);
            position_step = Vector2Add(
                position_step, Vector2Scale(creature.velocity, dt)
            );
            creature.position = Vector2Add(creature.position, position_step);
            if (fabs(position_step.x) > EPSILON) {
                creature.is_hflip = position_step.x < 0.0;
            }

            // -----------------------------------------------------------
            // compute collisions mtv and resolve it
            Rectangle *my_collider = creature.get_rigid_collider();
            creature.landed_at_speed = 0.0;
            if (my_collider) {
                // compute mtv
                Vector2 mtv = Vector2Zero();
                for (auto &collider : this->colliders) {
                    Vector2 collider_mtv = get_aabb_mtv(*my_collider, collider);

                    if (fabs(mtv.x) < fabs(collider_mtv.x)) {
                        mtv.x = collider_mtv.x;
                    }

                    if (fabs(mtv.y) < fabs(collider_mtv.y)) {
                        mtv.y = collider_mtv.y;
                    }
                }

                // resolve mtv
                creature.position = Vector2Add(creature.position, mtv);

                if (mtv.y < -EPSILON && creature.velocity.y > EPSILON) {
                    // hit the ground
                    creature.landed_at_speed = creature.velocity.y;
                    creature.velocity.y = 0.0;
                    creature.is_grounded = true;
                } else if (mtv.y > EPSILON && creature.velocity.y < -EPSILON) {
                    // hit the ceil
                    creature.velocity.y = 0.0;
                } else {
                    creature.is_grounded = false;
                }
            }
        }
    }

    void draw() {
        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode2D(this->camera.camera2d);

        // ---------------------------------------------------------------
        // draw tiled level
        int tile_width = this->tiled_level.meta["tilewidth"];
        int tile_height = this->tiled_level.meta["tileheight"];

        BeginShaderMode(this->shaders["sprite"]);
        for (auto &layer_json : this->tiled_level.meta["layers"]) {
            for (auto &chunk_json : layer_json["chunks"]) {
                int chunk_width = chunk_json["width"];
                int chunk_height = chunk_json["height"];
                int chunk_x = chunk_json["x"];
                int chunk_y = chunk_json["y"];

                auto &tile_ids = chunk_json["data"];
                for (int i = 0; i < tile_ids.size(); ++i) {
                    int tile_id = tile_ids[i];
                    if (tile_id == 0) continue;

                    int i_row = i / chunk_width;
                    int i_col = i % chunk_width;
                    float x = tile_width * (chunk_x + i_col);
                    float y = tile_height * (chunk_y + i_row);

                    Vector2 position = {.x = x, .y = y};

                    Sprite sprite = this->tiled_level.get_sprite(tile_id, position);
                    sprite.draw();
                }
            }
        }
        EndShaderMode();

        // ---------------------------------------------------------------
        // draw sprites
        BeginShaderMode(this->shaders["sprite"]);
        for (auto &creature : this->creatures) {
            Sprite sprite = creature.animator.get_sprite(
                creature.position, creature.is_hflip
            );
            sprite.draw();
        }
        EndShaderMode();

#if 1
        // ---------------------------------------------------------------
        // draw masks
        for (auto &creature : this->creatures) {
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
        for (auto &collider : this->colliders) {
            DrawRectangleRec(collider, ColorAlpha(RED, 0.2));
        }
#endif

        EndMode2D();

        EndDrawing();
    }
};

// -----------------------------------------------------------------------
// main loop
int main() {
    Game game;

    while (!WindowShouldClose()) {
        game.update();
        game.draw();
    }
}
