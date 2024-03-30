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

#define LANDING_MIN_SPEED 260
#define LANDING_DAMAGE_FACTOR 1.0
#define SAFE_DASHING_HEIGHT 24
#define ATTACK_0_AFTER_DASH_MIN_PROGRESS 0.5
#define ATTACK_1_AFTER_ATTACK_0_MIN_PROGRESS 0.5
#define ATTACK_2_AFTER_ATTACK_1_MIN_PROGRESS 0.5

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

class Collider {
  public:
    Rectangle mask;
    uint32_t id;
    void *owner = NULL;

    Collider()
        : mask()
        , id(0) {}

    Collider(Rectangle mask, int id)
        : mask(mask)
        , id(id) {}
};

class SpriteSheetAnimator {
  private:
    static uint32_t animation_id;

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
            this->animation_id += 1;
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

    Collider get_collider(std::string name, Vector2 position, bool is_hflip) {
        Sprite sprite = this->get_sprite(position, is_hflip);
        Rectangle *mask = sprite.get_mask(name);
        if (!mask) return Collider();

        return Collider(*mask, this->animation_id);
    }
};

uint32_t SpriteSheetAnimator::animation_id = 0;

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
    ATTACK_1,
    ATTACK_2,
    DEATH,
};

enum class CreatureType {
    PLAYER,
    BAT,
};

class Creature {
  public:
    CreatureType type;
    CreatureState state;
    SpriteSheetAnimator animator;

    float move_speed;
    float max_health;
    float health;
    float damage;

    Vector2 position = {0.0, 0.0};
    Vector2 velocity = {0.0, 0.0};

    bool is_hflip = false;
    bool is_grounded = false;
    bool has_weight = false;
    float landed_at_speed = 0.0;

    uint32_t prev_received_attack_id = 0;

    Creature(
        CreatureType type,
        CreatureState state,
        SpriteSheetAnimator animator,
        float move_speed,
        float max_health,
        float damage,
        Vector2 position,
        bool has_weight
    )
        : type(type)
        , state(state)
        , position(position)
        , move_speed(move_speed)
        , max_health(max_health)
        , health(max_health)
        , damage(damage)
        , animator(animator)
        , has_weight(has_weight) {}

    Collider get_rigid_collider() {
        Collider collider = this->animator.get_collider("rigid", position, is_hflip);
        collider.owner = this;
        return collider;
    }

    Collider get_attack_collider() {
        Collider collider = this->animator.get_collider("attack", position, is_hflip);
        collider.owner = this;
        return collider;
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
    std::vector<Collider> attack_colliders;
    std::vector<Collider> rigid_colliders;
    std::vector<Rectangle> static_rigid_rects;

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
        this->static_rigid_rects.clear();
        this->creatures.clear();

        this->tiled_level = TiledLevel(dir_path, name);

        for (auto layer_json : this->tiled_level.meta["layers"]) {
            std::string layer_name = std::string(layer_json["name"]);
            for (auto object : layer_json["objects"]) {
                auto object_x = object["x"];
                auto object_y = object["y"];
                auto object_width = object["width"];
                auto object_height = object["height"];

                for (auto property : object["properties"]) {

                    auto property_name = property["name"];
                    auto property_value = property["value"];

                    if (layer_name == "colliders" && property_name == "is_rigid"
                        && property_value) {
                        this->static_rigid_rects.push_back(
                            {.x = object_x,
                             .y = object_y,
                             .width = object_width,
                             .height = object_height}
                        );
                    } else if (layer_name == "creatures" && property_name == "type") {
                        if (property_value == "player") {
                            Creature player(
                                CreatureType::PLAYER,
                                CreatureState::IDLE,
                                SpriteSheetAnimator(&this->sprite_sheets["0"]),
                                90.0,
                                100.0,
                                50.0,
                                {.x = object_x, .y = object_y},
                                true
                            );
                            player.animator.play("knight_idle", 0.1, true);
                            this->creatures.push_back(player);
                            this->camera.camera2d.target = player.position;
                        } else if (property_value == "bat") {
                            Creature bat(
                                CreatureType::BAT,
                                CreatureState::IDLE,
                                SpriteSheetAnimator(&this->sprite_sheets["0"]),
                                100.0,
                                100.0,
                                50.0,
                                {.x = object_x, .y = object_y},
                                false
                            );
                            bat.animator.play("bat_flight", 0.1, true);
                            this->creatures.push_back(bat);
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
        if (IsKeyPressed(KEY_R)) {
            this->load_level("./resources/tiled/", "level_0");
            return;
        }

        float dt = GetFrameTime();

        this->camera.camera2d.target = this->creatures[0].position;
        this->attack_colliders.clear();
        this->rigid_colliders.clear();

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
            // update creature
            if (creature.type == CreatureType::PLAYER) {
                switch (creature.state) {
                    case CreatureState::IDLE:
                        // -> MOVING, JUMPING, FALLING, ATTACK_0
                        creature.animator.play("knight_idle", 0.1, true);

                        // move
                        if (IsKeyDown(KEY_D)) position_step.x += creature.move_speed * dt;
                        if (IsKeyDown(KEY_A)) position_step.x -= creature.move_speed * dt;

                        if (!creature.is_grounded) {
                            // -> FALLING
                            creature.state = CreatureState::FALLING;
                        } else if (IsKeyPressed(KEY_W)) {
                            // -> JUMPING
                            velocity_step.y -= 250.0;
                            creature.state = CreatureState::JUMPING;
                        } else if (IsKeyPressed(KEY_SPACE)) {
                            // -> ATTACK_0
                            creature.state = CreatureState::ATTACK_0;
                        } else if (position_step.x) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        }

                        break;
                    case CreatureState::MOVING:
                        // -> JUMPING, FALLING, DASHING, ATTACK_0
                        creature.animator.play("knight_run", 0.1, true);

                        // move
                        if (IsKeyDown(KEY_D)) position_step.x += creature.move_speed * dt;
                        if (IsKeyDown(KEY_A)) position_step.x -= creature.move_speed * dt;

                        if (!creature.is_grounded) {
                            // -> FALLING
                            creature.state = CreatureState::FALLING;
                        } else if (IsKeyPressed(KEY_W)) {
                            // -> JUMPING
                            velocity_step.y -= 250.0;
                            creature.state = CreatureState::JUMPING;
                        } else if (IsKeyPressed(KEY_LEFT_CONTROL)) {
                            // -> DASHING
                            float dir = creature.is_hflip ? -1.0 : 1.0;
                            creature.velocity.x = dir * creature.move_speed;
                            creature.state = CreatureState::DASHING;
                        } else if (IsKeyPressed(KEY_SPACE)) {
                            // -> ATTACK_0
                            creature.state = CreatureState::ATTACK_0;
                        } else if (!position_step.x) {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
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
                        // -> IDLE, MOVING, DASHING, LANDING
                        creature.animator.play("knight_fall", 0.1, false);

                        static float dash_pressed_at_y = -INFINITY;

                        // move
                        if (IsKeyDown(KEY_D)) position_step.x += creature.move_speed * dt;
                        if (IsKeyDown(KEY_A)) position_step.x -= creature.move_speed * dt;

                        // check if DASHING is pressed while FALLING
                        if (IsKeyPressed(KEY_LEFT_CONTROL)
                            && dash_pressed_at_y == -INFINITY) {
                            dash_pressed_at_y = creature.position.y;
                        }

                        // continue FALLING if not landed yet
                        if (creature.landed_at_speed == 0.0) break;

                        if (creature.landed_at_speed > 0.0
                            && creature.position.y - dash_pressed_at_y
                                   < SAFE_DASHING_HEIGHT) {
                            // -> DASHING

                            float dir = creature.is_hflip ? -1.0 : 1.0;
                            creature.velocity.x = dir * creature.move_speed;
                            creature.state = CreatureState::DASHING;
                        } else if (creature.landed_at_speed > LANDING_MIN_SPEED) {
                            // -> LANDING
                            creature.health -= LANDING_DAMAGE_FACTOR
                                               * (creature.landed_at_speed
                                                  - LANDING_MIN_SPEED);
                            creature.state = CreatureState::LANDING;
                        } else if (position_step.x) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        } else {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        }

                        // reset dash pressing event after finishing FALLING
                        dash_pressed_at_y = -INFINITY;

                        break;
                    case CreatureState::LANDING:
                        // -> IDLE
                        creature.animator.play("knight_landing", 0.1, false);

                        // -> IDLE
                        if (creature.animator.is_finished()) {
                            creature.state = CreatureState::IDLE;
                        }

                        break;
                    case CreatureState::DASHING:
                        // -> IDLE, FALLING, ATTACK_0
                        creature.animator.play("knight_roll", 0.1, false);

                        static float attack_0_pressed_at_progress = -INFINITY;

                        // check if ATTACK_0 is pressed while DASHING
                        if (IsKeyPressed(KEY_SPACE)
                            && attack_0_pressed_at_progress == -INFINITY) {
                            attack_0_pressed_at_progress = creature.animator.progress;
                        }

                        // continue DASHING if the animation is not finished yet
                        if (!creature.animator.is_finished()) break;

                        if (!creature.is_grounded) {
                            // -> FALLING
                            creature.state = CreatureState::FALLING;
                        } else if (attack_0_pressed_at_progress >= ATTACK_0_AFTER_DASH_MIN_PROGRESS) {
                            // -> ATTACK_0
                            creature.state = CreatureState::ATTACK_0;
                        } else {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        }

                        // reset attack_0 pressing event after finishing DASHING
                        attack_0_pressed_at_progress = -INFINITY;

                        // reset horizontal velocity after finishing DASHING
                        creature.velocity.x = 0.0;

                        break;
                    case CreatureState::ATTACK_0:
                        // -> IDLE, FALLING, ATTACK_1
                        creature.animator.play("knight_attack_0", 0.1, false);

                        static float attack_1_pressed_at_progress = -INFINITY;

                        // check if ATTACK_1 is pressed while ATTACK_0
                        if (IsKeyPressed(KEY_SPACE)
                            && attack_1_pressed_at_progress == -INFINITY) {
                            attack_1_pressed_at_progress = creature.animator.progress;
                        }

                        // continue ATTACK_0 if the animation is not finished yet
                        if (!creature.animator.is_finished()) break;

                        if (attack_1_pressed_at_progress
                            >= ATTACK_1_AFTER_ATTACK_0_MIN_PROGRESS) {
                            // -> ATTACK_1
                            creature.state = CreatureState::ATTACK_1;
                        } else if (!creature.is_grounded) {
                            // -> FALLING
                            creature.state = CreatureState::FALLING;
                        } else {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        }

                        // reset attack_1 pressing event after finishing ATTACK_0
                        attack_1_pressed_at_progress = -INFINITY;

                        break;
                    case CreatureState::ATTACK_1:
                        // -> IDLE, FALLING, ATTACK_2
                        creature.animator.play("knight_attack_1", 0.1, false);

                        static float attack_2_pressed_at_progress = -INFINITY;

                        // check if ATTACK_2 is pressed while ATTACK_1
                        if (IsKeyPressed(KEY_SPACE)
                            && attack_2_pressed_at_progress == -INFINITY) {
                            attack_2_pressed_at_progress = creature.animator.progress;
                        }

                        // continue ATTACK_1 if the animation is not finished yet
                        if (!creature.animator.is_finished()) break;

                        if (attack_2_pressed_at_progress
                            >= ATTACK_2_AFTER_ATTACK_1_MIN_PROGRESS) {
                            // -> ATTACK_2
                            creature.state = CreatureState::ATTACK_2;
                        } else if (!creature.is_grounded) {
                            // -> FALLING
                            creature.state = CreatureState::FALLING;
                        } else {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        }

                        // reset attack_2 pressing event after finishing ATTACK_1
                        attack_2_pressed_at_progress = -INFINITY;

                        break;
                    case CreatureState::ATTACK_2:
                        // -> IDLE, FALLING
                        creature.animator.play("knight_attack_2", 0.1, false);

                        // continue ATTACK_2 if the animation is not finished yet
                        if (!creature.animator.is_finished()) break;

                        if (!creature.is_grounded) {
                            // -> FALLING
                            creature.state = CreatureState::FALLING;
                        } else {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        }

                        break;
                    case CreatureState::DEATH:
                        creature.animator.play("knight_death", 0.1, false);

                        break;
                    default: break;
                }

                // -> DEATH
                if (creature.health <= 0.0) {
                    creature.state = CreatureState::DEATH;
                }
            } else if (creature.type == CreatureType::BAT) {
                switch (creature.state) {
                    case CreatureState::IDLE: break;
                    default: break;
                }
            }

            // -----------------------------------------------------------
            // reset single-frame values
            creature.landed_at_speed = 0.0;

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
            // push colliders
            this->attack_colliders.push_back(creature.get_attack_collider());
            this->rigid_colliders.push_back(creature.get_rigid_collider());
        }

        // -----------------------------------------------------------
        // resolve colliders
        for (Collider &rigid_collider : this->rigid_colliders) {
            if (!rigid_collider.id) continue;

            Creature *rigid_creature = (Creature *)rigid_collider.owner;

            // compute mtv
            Vector2 mtv = Vector2Zero();
            for (auto &rect : this->static_rigid_rects) {
                Vector2 collider_mtv = get_aabb_mtv(rigid_collider.mask, rect);

                if (fabs(mtv.x) < fabs(collider_mtv.x)) {
                    mtv.x = collider_mtv.x;
                }

                if (fabs(mtv.y) < fabs(collider_mtv.y)) {
                    mtv.y = collider_mtv.y;
                }
            }

            // resolve mtv
            rigid_creature->position = Vector2Add(rigid_creature->position, mtv);

            if (mtv.y < -EPSILON && rigid_creature->velocity.y > EPSILON) {
                // hit the ground
                rigid_creature->landed_at_speed = rigid_creature->velocity.y;
                rigid_creature->velocity.y = 0.0;
                rigid_creature->is_grounded = true;
            } else if (mtv.y > EPSILON && rigid_creature->velocity.y < -EPSILON) {
                // hit the ceil
                rigid_creature->velocity.y = 0.0;
            } else {
                rigid_creature->is_grounded = false;
            }

            // resolve against attack colliders
            for (Collider &attack_collider : this->attack_colliders) {
                if (!attack_collider.id) continue;

                Creature *attacker_creature = (Creature *)attack_collider.owner;

                // creature can't attack itself
                if (attacker_creature == rigid_creature) continue;

                // one of the creatures must be the PLAYER
                if (rigid_creature->type != CreatureType::PLAYER
                    && attacker_creature->type != CreatureType::PLAYER)
                    continue;

                // ignore already received attack
                if (rigid_creature->prev_received_attack_id == attack_collider.id)
                    continue;

                // colliders must overlap
                if (!CheckCollisionRecs(rigid_collider.mask, attack_collider.mask))
                    continue;

                std::cout << "hit: " << attack_collider.id << "\n";
                rigid_creature->prev_received_attack_id = attack_collider.id;
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

            mask = sprite.get_mask("attack");
            if (mask) {
                DrawRectangleRec(*mask, ColorAlpha(YELLOW, 0.2));
            }
        }

        // ---------------------------------------------------------------
        // draw colliders
        for (auto &collider : this->static_rigid_rects) {
            DrawRectangleRec(collider, ColorAlpha(RED, 0.2));
        }
#endif

        EndMode2D();

        // ---------------------------------------------------------------
        // draw ui
        Creature &player = this->creatures[0];

        // healthbar
        float health_ratio = player.health / player.max_health;
        int max_bar_width = 300;
        int bar_width = health_ratio * max_bar_width;
        DrawRectangle(5, 5, bar_width, 30, RED);

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
