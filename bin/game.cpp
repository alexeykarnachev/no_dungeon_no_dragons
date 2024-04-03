#include "json.hpp"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <asm-generic/errno.h>
#include <cmath>
#include <cstdio>
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

#define VIEW_LINE_Y_OFFSET -16

#define GRAVITY 400
#define X_FRICTION 100

#define LANDING_MIN_SPEED 260
#define LANDING_DAMAGE_FACTOR 1.0
#define SAFE_DASHING_HEIGHT 24
#define ATTACK_0_AFTER_DASH_MIN_PROGRESS 0.5
#define ATTACK_1_AFTER_ATTACK_0_MIN_PROGRESS 0.5
#define ATTACK_2_AFTER_ATTACK_1_MIN_PROGRESS 0.5

#define CREATURE_VIEW_DISTANCE 200
#define CREATURE_MAX_VIEW_ANGLE 20

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
// geometry and collisions
Vector2 get_rect_center(Rectangle rect) {
    return {.x = rect.x + 0.5f * rect.width, .y = rect.y + 0.5f * rect.height};
}

float get_line_angle(Vector2 start, Vector2 end) {
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float slope = dy / dx;

    float angle_radians = atanf(fabsf(slope));
    float angle_degrees = angle_radians * (180.0f / M_PI);

    if (angle_degrees > 90.0f) {
        angle_degrees = 90.0f - (angle_degrees - 90.0f);
    }

    return angle_degrees;
}

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

bool check_collision_rect_line(Rectangle rect, Vector2 start, Vector2 end) {
    Vector2 rect_tl = {rect.x, rect.y};
    Vector2 rect_tr = {rect.x + rect.width, rect.y};
    Vector2 rect_br = {rect.x + rect.width, rect.y + rect.height};
    Vector2 rect_bl = {rect.x, rect.y + rect.height};

    Vector2 point;
    return CheckCollisionLines(start, end, rect_tl, rect_tr, &point)
           || CheckCollisionLines(start, end, rect_tr, rect_br, &point)
           || CheckCollisionLines(start, end, rect_br, rect_bl, &point)
           || CheckCollisionLines(start, end, rect_bl, rect_tl, &point);
}

// -----------------------------------------------------------------------
// sprite
Rectangle rect_from_json(json data) {
    Rectangle rect = {
        .x = data["x"], .y = data["y"], .width = data["w"], .height = data["h"]};

    return rect;
}

enum class PivotType {
    CENTER_BOTTOM,
    LEFT_CENTER,
    RIGHT_CENTER,
    CENTER_CENTER,
};

class Pivot {
  public:
    PivotType type;
    Vector2 position;

    Pivot(PivotType type, Vector2 position)
        : type(type)
        , position(position) {}
};

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

    Sprite(json frame_json, Texture2D texture, Pivot pivot, bool is_hflip)
        : texture(texture) {

        // flip sprite pivot if is_hflip = true
        if (is_hflip) {
            if (pivot.type == PivotType::LEFT_CENTER) {
                pivot.type = PivotType::RIGHT_CENTER;
            } else if (pivot.type == PivotType::RIGHT_CENTER) {
                pivot.type = PivotType::LEFT_CENTER;
            }
        }

        json sprite_json = frame_json["sprite"];
        json masks_json = frame_json["masks"];

        Rectangle src = rect_from_json(sprite_json);

        // find sprite's top right corner (because that's how sprites are rendered)
        Vector2 offset;
        switch (pivot.type) {
            case PivotType::CENTER_BOTTOM:
                offset = {-0.5f * src.width, -src.height};
                break;
            case PivotType::LEFT_CENTER: offset = {0.0, -0.5f * src.height}; break;
            case PivotType::RIGHT_CENTER:
                offset = {-src.width, -0.5f * src.height};
                break;
            case PivotType::CENTER_CENTER:
                offset = {-0.5f * src.width, -0.5f * src.height};
                break;
        }

        Rectangle dst = src;

        dst.x = pivot.position.x + offset.x;
        dst.y = pivot.position.y + offset.y;

        std::unordered_map<std::string, Rectangle> masks;
        for (auto it = masks_json.begin(); it != masks_json.end(); ++it) {
            const auto &mask_name = it.key();
            const auto &mask_json = it.value();

            Rectangle mask = rect_from_json(mask_json);
            mask.y = dst.y + mask.y;
            mask.x = is_hflip ? dst.x - mask.x + src.width - mask.width : dst.x + mask.x;
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

    Sprite get_sprite(std::string &name, int idx, Pivot pivot, bool is_hflip) {
        json frame_json = meta["frames"][name][idx];
        Sprite sprite(frame_json, this->texture, pivot, is_hflip);
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

    Collider()
        : mask()
        , id(0) {}

    Collider(Rectangle mask, int id)
        : mask(mask)
        , id(id) {}
};

class SpriteSheetAnimator {
  private:
    static uint32_t global_animation_id;
    uint32_t animation_id;

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
            this->animation_id = ++this->global_animation_id;
        }
    }

    void update(float dt) {
        if (this->name.empty()) return;

        int n_frames = this->sprite_sheet->count_frames(this->name);

        this->progress += dt / (n_frames * this->frame_duration);
        if (this->is_repeat) {
            if (this->progress >= 1.0) {
                this->animation_id = ++this->global_animation_id;
            }
            this->progress -= std::floor(this->progress);
        } else {
            this->progress = std::fmin(this->progress, 1.0);
        }
    }

    bool is_finished() {
        return this->progress == 1.0 && !this->is_repeat;
    }

    Sprite get_sprite(Pivot pivot, bool is_hflip) {
        if (this->name.empty()) {
            return Sprite();
        }

        int n_frames = this->sprite_sheet->count_frames(this->name);
        int idx = std::round(this->progress * (n_frames - 1.0));
        Sprite sprite = this->sprite_sheet->get_sprite(this->name, idx, pivot, is_hflip);
        return sprite;
    }

    Collider get_collider(std::string name, Pivot pivot, bool is_hflip) {
        Sprite sprite = this->get_sprite(pivot, is_hflip);
        Rectangle *mask = sprite.get_mask(name);
        if (!mask) return Collider();

        return Collider(*mask, this->animation_id);
    }
};

uint32_t SpriteSheetAnimator::global_animation_id = 0;

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
    DELETE,
};

enum class CreatureType {
    SPRITE,
    PLAYER,
    BAT,
    WOLF,
    GOLEM,
};

class Creature {
  private:
    PivotType sprite_pivot_type = PivotType::CENTER_BOTTOM;

  public:
    CreatureType type;
    CreatureState state;
    SpriteSheetAnimator animator;

    float move_speed;
    float max_health;
    float health;
    float damage;
    float attack_distance;
    bool can_view_vertically;

    Vector2 position = {0.0, 0.0};
    Vector2 velocity = {0.0, 0.0};

    bool is_hflip = false;
    bool is_grounded = false;
    bool is_flying = false;
    bool can_see_player = false;
    bool can_attack_player = false;
    float landed_at_speed = 0.0;
    float last_received_damage_time = -1.0;
    uint32_t last_received_attack_id = 0;

    Creature() {}

    Creature(
        CreatureType type,
        CreatureState state,
        SpriteSheetAnimator animator,
        float move_speed,
        float max_health,
        float damage,
        float attack_distance,
        bool can_view_vertically,
        Vector2 position
    )
        : type(type)
        , state(state)
        , animator(animator)
        , move_speed(move_speed)
        , max_health(max_health)
        , damage(damage)
        , attack_distance(attack_distance)
        , can_view_vertically(can_view_vertically)
        , position(position)
        , health(max_health) {}

    static Creature create_sprite(
        SpriteSheetAnimator animator,
        Vector2 position,
        bool is_hflip,
        PivotType sprite_pivot_type
    ) {
        Creature sprite;
        sprite.type = CreatureType::SPRITE;
        sprite.state = CreatureState::IDLE;
        sprite.animator = animator;
        sprite.position = position;
        sprite.is_hflip = is_hflip;
        sprite.sprite_pivot_type = sprite_pivot_type;
        sprite.is_flying = true;

        return sprite;
    }

    int get_view_dir() {
        return this->is_hflip ? -1 : 1;
    }

    Pivot get_pivot() {
        return Pivot(this->sprite_pivot_type, this->position);
    }

    Sprite get_sprite() {
        Sprite sprite = this->animator.get_sprite(this->get_pivot(), this->is_hflip);
        return sprite;
    }

    Collider get_rigid_collider() {
        Collider collider = this->animator.get_collider(
            "rigid", this->get_pivot(), is_hflip
        );
        return collider;
    }

    Collider get_attack_collider() {
        Collider collider = this->animator.get_collider(
            "attack", this->get_pivot(), is_hflip
        );
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
    std::vector<Rectangle> static_rigid_rects;

    GameCamera camera;

    float dt = 0.0;
    float time = 0.0;

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
                        Creature creature;
                        if (property_value == "player") {
                            creature = Creature(
                                CreatureType::PLAYER,
                                CreatureState::IDLE,
                                SpriteSheetAnimator(&this->sprite_sheets["0"]),
                                100.0,
                                1000.0,
                                50.0,
                                0.0,
                                true,
                                {.x = object_x, .y = object_y}
                            );
                            this->camera.camera2d.target = creature.position;
                        } else if (property_value == "bat") {
                            creature = Creature(
                                CreatureType::BAT,
                                CreatureState::IDLE,
                                SpriteSheetAnimator(&this->sprite_sheets["0"]),
                                50.0,
                                300.0,
                                50.0,
                                25.0,
                                true,
                                {.x = object_x, .y = object_y}
                            );
                        } else if (property_value == "wolf") {
                            creature = Creature(
                                CreatureType::WOLF,
                                CreatureState::IDLE,
                                SpriteSheetAnimator(&this->sprite_sheets["0"]),
                                80.0,
                                300.0,
                                50.0,
                                35.0,
                                false,
                                {.x = object_x, .y = object_y}
                            );
                        } else if (property_value == "golem") {
                            creature = Creature(
                                CreatureType::GOLEM,
                                CreatureState::IDLE,
                                SpriteSheetAnimator(&this->sprite_sheets["0"]),
                                60.0,
                                400.0,
                                50.0,
                                35.0,
                                false,
                                {.x = object_x, .y = object_y}
                            );
                        }

                        this->creatures.push_back(creature);
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

        this->dt = GetFrameTime();
        this->time += this->dt;
        this->camera.camera2d.target = this->creatures[0].position;

        Creature &player = this->creatures[0];
        for (auto &creature : this->creatures) {
            creature.animator.update(this->dt);

            // -----------------------------------------------------------
            // immediate velocity and position needs to be computed by
            // the Character update logic
            Vector2 position_step = Vector2Zero();

            // -----------------------------------------------------------
            // update creature
            if (creature.type == CreatureType::PLAYER) {
                // -> DEATH
                if (creature.state != CreatureState::DEATH && creature.health <= 0.0) {
                    creature.state = CreatureState::DEATH;
                }

                switch (creature.state) {
                    case CreatureState::IDLE:
                        // -> MOVING, JUMPING, FALLING, ATTACK_0
                        creature.animator.play("knight_idle", 0.1, true);

                        // move
                        if (IsKeyDown(KEY_D))
                            position_step.x += creature.move_speed * this->dt;
                        if (IsKeyDown(KEY_A))
                            position_step.x -= creature.move_speed * this->dt;

                        if (!creature.is_grounded) {
                            // -> FALLING
                            creature.state = CreatureState::FALLING;
                        } else if (IsKeyPressed(KEY_W)) {
                            // -> JUMPING
                            creature.velocity.y = -250.0;
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
                        if (IsKeyDown(KEY_D))
                            position_step.x += creature.move_speed * this->dt;
                        if (IsKeyDown(KEY_A))
                            position_step.x -= creature.move_speed * this->dt;

                        if (!creature.is_grounded) {
                            // -> FALLING
                            creature.state = CreatureState::FALLING;
                        } else if (IsKeyPressed(KEY_W)) {
                            // -> JUMPING
                            creature.velocity.y = -250.0;
                            creature.state = CreatureState::JUMPING;
                        } else if (IsKeyPressed(KEY_LEFT_CONTROL)) {
                            // -> DASHING
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
                        if (IsKeyDown(KEY_D))
                            position_step.x += creature.move_speed * this->dt;
                        if (IsKeyDown(KEY_A))
                            position_step.x -= creature.move_speed * this->dt;

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
                        if (IsKeyDown(KEY_D))
                            position_step.x += creature.move_speed * this->dt;
                        if (IsKeyDown(KEY_A))
                            position_step.x -= creature.move_speed * this->dt;

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
                            creature.state = CreatureState::DASHING;
                        } else if (creature.landed_at_speed > LANDING_MIN_SPEED) {
                            // -> LANDING
                            creature.health -= LANDING_DAMAGE_FACTOR
                                               * (creature.landed_at_speed
                                                  - LANDING_MIN_SPEED);
                            creature.last_received_damage_time = this->time;
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

                        if (!creature.animator.is_finished()) {
                            position_step.x += creature.get_view_dir()
                                               * creature.move_speed * this->dt;

                            // continue DASHING if the animation is not finished yet
                            break;
                        }

                        static float attack_0_pressed_at_progress = -INFINITY;

                        // check if ATTACK_0 is pressed while DASHING
                        if (IsKeyPressed(KEY_SPACE)
                            && attack_0_pressed_at_progress == -INFINITY) {
                            attack_0_pressed_at_progress = creature.animator.progress;
                        }

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
            } else if (creature.type == CreatureType::BAT) {
                creature.is_flying = creature.health > EPSILON
                                     && fabs(creature.velocity.x) < EPSILON;

                // -> FALLING (if not dead, always falls if gravity should be applied)
                if (creature.state != CreatureState::DEATH && !creature.is_flying) {
                    creature.state = CreatureState::FALLING;
                }

                switch (creature.state) {
                    case CreatureState::IDLE:
                        // -> MOVING, ATTACK_0
                        creature.animator.play("bat_flight", 0.04, true);

                        if (creature.can_attack_player) {
                            // -> ATTACK_0
                            creature.state = CreatureState::ATTACK_0;
                        } else if (creature.can_see_player) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        }

                        break;
                    case CreatureState::MOVING:
                        // -> IDLE, ATTACK_0
                        creature.animator.play("bat_flight", 0.04, true);

                        if (creature.can_attack_player) {
                            // -> ATTACK_0
                            creature.state = CreatureState::ATTACK_0;
                        } else if (creature.can_see_player) {
                            position_step = this->get_step_towards_player(creature);
                        } else {
                            creature.state = CreatureState::IDLE;
                        };

                        break;
                    case CreatureState::ATTACK_0:
                        // -> IDLE, MOVING
                        creature.animator.play("bat_attack", 0.1, true);

                        if (!creature.can_see_player) {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        } else if (!creature.can_attack_player && creature.animator.progress < 0.3) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        }

                        break;
                    case CreatureState::FALLING:
                        // -> IDLE, DEATH
                        creature.animator.play("bat_fall", 0.1, false);

                        if (creature.health <= 0.0 && creature.animator.is_finished()
                            && creature.is_grounded) {
                            creature.state = CreatureState::DEATH;
                        } else if (creature.is_flying) {
                            creature.state = CreatureState::IDLE;
                        }

                        break;
                    case CreatureState::DEATH:
                        creature.animator.play("bat_death", 0.1, false);

                        break;
                    default: break;
                }
            } else if (creature.type == CreatureType::WOLF) {
                // -> DEATH
                if (creature.state != CreatureState::DEATH && creature.health <= 0.0) {
                    creature.state = CreatureState::DEATH;
                }

                switch (creature.state) {
                    case CreatureState::IDLE:
                        // -> MOVING, ATTACK_0
                        creature.animator.play("wolf_idle", 0.1, true);

                        if (creature.can_attack_player) {
                            // -> ATTACK_0
                            creature.state = CreatureState::ATTACK_0;
                        } else if (creature.can_see_player) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        }

                        break;
                    case CreatureState::MOVING:
                        // -> IDLE, ATTACK_0
                        creature.animator.play("wolf_run", 0.1, true);

                        if (creature.can_attack_player) {
                            // -> ATTACK_0
                            creature.state = CreatureState::ATTACK_0;
                        } else if (creature.can_see_player) {
                            position_step = this->get_step_towards_player(creature);
                        } else {
                            creature.state = CreatureState::IDLE;
                        };

                        break;
                    case CreatureState::ATTACK_0:
                        // -> IDLE, MOVING
                        creature.animator.play("wolf_attack", 0.1, true);

                        if (!creature.can_see_player) {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        } else if (!creature.can_attack_player && creature.animator.progress < 0.3) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        }

                        break;
                    case CreatureState::DEATH:
                        creature.animator.play("wolf_death", 0.1, false);

                        break;
                    default: break;
                }
            } else if (creature.type == CreatureType::GOLEM) {
                // -> DEATH
                if (creature.state != CreatureState::DEATH && creature.health <= 0.0) {
                    creature.state = CreatureState::DEATH;
                }

                switch (creature.state) {
                    case CreatureState::IDLE:
                        // -> MOVING, ATTACK_0
                        creature.animator.play("golem_idle", 0.1, true);

                        if (creature.can_attack_player) {
                            // -> ATTACK_0
                            creature.state = CreatureState::ATTACK_0;
                        } else if (creature.can_see_player) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        }

                        break;
                    case CreatureState::MOVING:
                        // -> IDLE, ATTACK_0
                        creature.animator.play("golem_run", 0.1, true);

                        if (creature.can_attack_player) {
                            // -> ATTACK_0
                            creature.state = CreatureState::ATTACK_0;
                        } else if (creature.can_see_player) {
                            position_step = this->get_step_towards_player(creature);
                        } else {
                            creature.state = CreatureState::IDLE;
                        };

                        break;
                    case CreatureState::ATTACK_0:
                        // -> IDLE, MOVING
                        creature.animator.play("golem_attack", 0.1, true);

                        if (!creature.can_see_player) {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        } else if (!creature.can_attack_player && creature.animator.progress < 0.3) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        }

                        break;
                    case CreatureState::DEATH:
                        creature.animator.play("golem_death", 0.1, false);

                        break;
                    default: break;
                }
            } else if (creature.type == CreatureType::SPRITE) {
                if (creature.animator.is_finished()) {
                    creature.state = CreatureState::DELETE;
                }
            }

            // -----------------------------------------------------------
            // reset single-frame values
            creature.landed_at_speed = 0.0;

            // -----------------------------------------------------------
            // apply gravity and x friction
            creature.velocity.y += this->dt * GRAVITY;
            if (creature.is_flying) {
                creature.velocity = Vector2Zero();
            } else if (fabs(creature.velocity.x) < this->dt * X_FRICTION) {
                creature.velocity.x = 0.0;
            } else if (creature.velocity.x > 0.0) {
                creature.velocity.x -= this->dt * X_FRICTION;
            } else {
                creature.velocity.x += this->dt * X_FRICTION;
            }

            // don't apply immediate position step if creature is under
            // some velocity effect
            if (fabs(creature.velocity.y) > EPSILON) {
                position_step.y = 0.0;
            }
            if (fabs(creature.velocity.x) > EPSILON) {
                position_step.x = 0.0;
            }

            // flip creature only if position_step.x != 0
            if (fabs(position_step.x) > EPSILON) {
                creature.is_hflip = position_step.x < 0.0;
            }

            position_step = Vector2Add(
                position_step, Vector2Scale(creature.velocity, this->dt)
            );
            creature.position = Vector2Add(creature.position, position_step);
        }

        // -----------------------------------------------------------
        // resolve colliders
        for (Creature &rigid_creature : this->creatures) {
            Collider rigid_collider = rigid_creature.get_rigid_collider();
            if (!rigid_collider.id) continue;

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
            rigid_creature.position = Vector2Add(rigid_creature.position, mtv);

            if (mtv.y < -EPSILON && rigid_creature.velocity.y > EPSILON) {
                // hit the ground
                rigid_creature.landed_at_speed = rigid_creature.velocity.y;
                rigid_creature.velocity = Vector2Zero();
                rigid_creature.is_grounded = true;
            } else if (mtv.y > EPSILON && rigid_creature.velocity.y < -EPSILON) {
                // hit the ceil
                rigid_creature.velocity.y = 0.0;
            } else {
                rigid_creature.is_grounded = false;
            }

            // resolve attack colliders
            for (Creature &attacker_creature : this->creatures) {
                Collider attacker_collider = attacker_creature.get_attack_collider();
                if (!attacker_collider.id) continue;

                // creature can't attack itself
                if (&attacker_creature == &rigid_creature) continue;

                // creature is already dead
                if (rigid_creature.health <= 0.0) continue;

                // one of the creatures must be the PLAYER
                if (rigid_creature.type != CreatureType::PLAYER
                    && attacker_creature.type != CreatureType::PLAYER)
                    continue;

                // ignore already received attack
                if (rigid_creature.last_received_attack_id == attacker_collider.id)
                    continue;

                // colliders must overlap
                if (!CheckCollisionRecs(rigid_collider.mask, attacker_collider.mask))
                    continue;

                rigid_creature.health -= attacker_creature.damage;
                rigid_creature.last_received_attack_id = attacker_collider.id;
                rigid_creature.last_received_damage_time = this->time;

                // apply knock back
                rigid_creature.velocity = {
                    .x = attacker_creature.get_view_dir() * 75.0f, .y = -75.0f};
            }
        }

        // -----------------------------------------------------------
        // update can_see_player, can_attack_player
        for (Creature &creature : this->creatures) {
            creature.can_see_player = false;
            creature.can_attack_player = false;

            Vector2 view_line_start = creature.position;
            Vector2 view_line_end = player.position;
            float dist = Vector2Distance(view_line_start, view_line_end);

            // can't see and can't attack the dead player.
            if (player.health <= 0.0) continue;

            // PLAYER can't see or attack himself
            if (creature.type == CreatureType::PLAYER) continue;

            // creature can't see or attack the PLAYER if its allowed view angle
            // is restricted (can_view_vertically = false) and the actual
            // view angle is too large
            if (!creature.can_view_vertically
                && get_line_angle(view_line_start, view_line_end)
                       > CREATURE_MAX_VIEW_ANGLE) {
                continue;
            }

            // creature can't see the PLAYER if the PLAYER is too far away
            if (dist > CREATURE_VIEW_DISTANCE) continue;

            // creatures positions usually touches the ground, so
            // I offset them to prevent the view ray always collid with
            // the ground.
            // TODO: I could compute the middle point of the colliders,
            // but they may be not present. Or I can factor out this
            // offset into a separate creature parameter (e.g eyes_offset).
            view_line_start.y += VIEW_LINE_Y_OFFSET;
            view_line_end.y += VIEW_LINE_Y_OFFSET;

            // can_see_player
            for (auto &rect : this->static_rigid_rects) {
                creature.can_see_player = !check_collision_rect_line(
                    rect, view_line_start, view_line_end
                );
                if (!creature.can_see_player) break;
            }

            // can_attack_player
            if (creature.can_see_player) {
                creature.can_attack_player = dist < creature.attack_distance;
            }
        }

        // -----------------------------------------------------------
        // clean up DELETE creatures
        int free_idx = -1;
        for (int i = 0; i < this->creatures.size(); ++i) {
            Creature &creature = this->creatures[i];
            if (creature.state == CreatureState::DELETE && free_idx == -1) {
                free_idx = i;
            } else if (creature.state != CreatureState::DELETE && free_idx != -1) {
                this->creatures[free_idx++] = creature;
            }
        }
        if (free_idx != -1) {
            this->creatures.resize(free_idx);
        }
    }

    void draw() {
        // ---------------------------------------------------------------
        // sort sprite before rendering
        static std::vector<Sprite> normal_sprites;
        static std::vector<Sprite> attacked_sprites;
        normal_sprites.clear();
        attacked_sprites.clear();

        // tiled level
        // TODO: could be done once, maybe factor out into tiled_level object?
        int tile_width = this->tiled_level.meta["tilewidth"];
        int tile_height = this->tiled_level.meta["tileheight"];
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
                    normal_sprites.push_back(sprite);
                }
            }
        }

        // creatures
        for (Creature &creature : this->creatures) {
            Sprite sprite = creature.get_sprite();
            float t = creature.last_received_damage_time;
            if (t > 0.0 && this->time - t < 0.1) {
                attacked_sprites.push_back(sprite);
            } else {
                normal_sprites.push_back(sprite);
            }
        }

        // ---------------------------------------------------------------
        // draw sprites
        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode2D(this->camera.camera2d);

        this->draw_sprites(normal_sprites, BLANK);
        this->draw_sprites(attacked_sprites, WHITE);
#if 0
        // ---------------------------------------------------------------
        // draw masks
        for (auto &creature : this->creatures) {
            DrawCircle(creature.position.x, creature.position.y, 2, RED);
        }

        for (auto &creature : this->creatures) {
            Sprite sprite = creature.get_sprite();
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

    Vector2 get_step_towards_player(Creature &creature) {
        Creature &player = this->creatures[0];
        if (&creature == &player) return {0.0, 0.0};

        Vector2 start = creature.position;
        Vector2 end = player.position;
        Vector2 dir = Vector2Normalize(Vector2Subtract(end, start));
        Vector2 step = Vector2Scale(dir, creature.move_speed * this->dt);

        return step;
    }

    void draw_sprites(std::vector<Sprite> sprites, Color plain_color) {
        Shader sprite_shader = this->shaders["sprite"];

        BeginShaderMode(sprite_shader);

        Vector4 color = ColorNormalize(plain_color);
        SetShaderValue(
            sprite_shader,
            GetShaderLocation(sprite_shader, "plain_color"),
            &color,
            SHADER_UNIFORM_VEC4
        );
        for (Sprite &sprite : sprites) {
            sprite.draw();
        }

        EndShaderMode();
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
