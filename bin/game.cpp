#include "json.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <asm-generic/errno.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

#define HASHMAP_GET_OR_NULL(map, key) \
    ((map).find(key) != (map).end() ? &((map)[key]) : nullptr)

// #define LEVELS_DIR "./resources/tiled/levels/"
// #define LEVEL "level_1"
#define LEVELS_DIR "./resources/tiled/"
#define LEVEL "level_0"

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define SHADOW_MAP_WIDTH 512
#define SHADOW_MAP_HEIGHT 512
#define MAX_N_LIGHTS 32

#define VIEW_LINE_Y_OFFSET -16

#define GRAVITY 700
#define X_FRICTION 100

#define LANDING_MIN_SPEED 260
#define LANDING_DAMAGE_FACTOR 1.0
#define SAFE_DASHING_HEIGHT 24
#define SUCCESSFUL_BLOCK_MIN_PROGRESS 0.5
#define ATTACK_0_AFTER_DASH_MIN_PROGRESS 0.5
#define ATTACK_1_AFTER_ATTACK_0_MIN_PROGRESS 0.5
#define ATTACK_2_AFTER_ATTACK_1_MIN_PROGRESS 0.5

#define ATTACK_0_FRAME_DURATION 0.07
#define ATTACK_1_FRAME_DURATION 0.07
#define ATTACK_2_FRAME_DURATION 0.07

#define CREATURE_VIEW_DISTANCE 200
#define CREATURE_MAX_VIEW_ANGLE 20

#define PLATFORM_SPEED 50

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
#define LEFT (1 << 0)
#define TOP (1 << 1)
#define RIGHT (1 << 2)
#define BOT (1 << 3)

typedef struct Triangle {
    Vector2 a;
    Vector2 b;
    Vector2 c;
} Triangle;

typedef struct Line {
    Vector2 a;
    Vector2 b;
} Line;

typedef struct RectDetailed {
    Vector2 lt;  // left top
    Vector2 rt;  // right top
    Vector2 rb;  // right bot
    Vector2 lb;  // left bot

    Vector2 lm;  // left mid
    Vector2 rm;  // right mid
    Vector2 mt;  // mid top
    Vector2 mb;  // mid bot

    Line left;
    Line top;
    Line right;
    Line bot;

    Line midh;
    Line midv;
} RectDetailed;

Rectangle erode_rect(Rectangle rect, float val) {
    return {
        .x = rect.x + val,
        .y = rect.y + val,
        .width = rect.width - 2.0f * val,
        .height = rect.height - 2.0f * val};
}

RectDetailed get_rect_detailed(Rectangle rect) {
    Vector2 lt = {rect.x, rect.y};
    Vector2 rt = {rect.x + rect.width, rect.y};
    Vector2 rb = {rect.x + rect.width, rect.y + rect.height};
    Vector2 lb = {rect.x, rect.y + rect.height};

    Vector2 lm = {rect.x, rect.y + 0.5f * rect.height};
    Vector2 rm = {rect.x + rect.width, rect.y + 0.5f * rect.height};
    Vector2 mt = {rect.x + 0.5f * rect.width, rect.y};
    Vector2 mb = {rect.x + 0.5f * rect.width, rect.y + rect.height};
    return {
        .lt = lt,
        .rt = rt,
        .rb = rb,
        .lb = lb,
        .lm = lm,
        .rm = rm,
        .mt = mt,
        .mb = mb,
        .left = {lt, lb},
        .top = {lt, rt},
        .right = {rt, rb},
        .bot = {lb, rb},
        .midh = {lm, rm},
        .midv = {mt, mb},
    };
}

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
    // TODO: factor out into RectangleVerts
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

bool check_collision_lines(Line line0, Line line1) {
    Vector2 point;
    return CheckCollisionLines(line0.a, line0.b, line1.a, line1.b, &point);
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
    CENTER_TOP,
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

        // find sprite's top left corner (because that's how sprites are rendered)
        Vector2 offset;
        switch (pivot.type) {
            case PivotType::CENTER_BOTTOM:
                offset = {-0.5f * src.width, -src.height};
                break;
            case PivotType::CENTER_TOP: offset = {-0.5f * src.width, 0.0}; break;
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
        if (!meta["frames"].contains(name)) {
            return 0;
        }
        return meta["frames"][name].size();
    }

    Sprite get_sprite(std::string &name, int idx, Pivot pivot, bool is_hflip) {
        if (!meta["frames"].contains(name)) {
            return Sprite();
        }
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
    std::string base_name = "";
    std::string animation_name = "";
    float frame_duration = 0.0;
    bool is_repeat = true;

    std::string get_full_name() {
        if (this->animation_name.empty()) {
            return this->base_name;
        } else {
            return this->base_name + "_" + this->animation_name;
        }
    }

  public:
    float progress = 0.0;
    SpriteSheetAnimator() {}
    SpriteSheetAnimator(SpriteSheet *sprite_sheet, std::string base_name)
        : sprite_sheet(sprite_sheet)
        , base_name(base_name) {}

    void play(std::string animation_name, float frame_duration, bool is_repeat) {
        this->frame_duration = frame_duration;
        this->is_repeat = is_repeat;

        if (this->animation_name != animation_name) {
            this->animation_name = animation_name;
            this->progress = 0.0;
            this->animation_id = ++this->global_animation_id;
        }
    }

    void play(float frame_duration, bool is_repeat) {
        return this->play("", frame_duration, is_repeat);
    }

    void update(float dt) {
        std::string name = this->get_full_name();

        int n_frames = this->sprite_sheet->count_frames(name);

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
        std::string name = this->get_full_name();

        int n_frames = this->sprite_sheet->count_frames(name);
        int idx = std::round(this->progress * (n_frames - 1.0));
        Sprite sprite = this->sprite_sheet->get_sprite(name, idx, pivot, is_hflip);
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

        std::string texture_file_path = LEVELS_DIR + std::string(meta["image"]);
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
  public:
    float view_width;
    float aspect;
    Vector2 target;

    GameCamera() = default;
    GameCamera(float view_width, float aspect)
        : view_width(view_width)
        , aspect(aspect) {}

    Rectangle get_screen_rect() {
        float view_height = this->view_width / this->aspect;
        Vector2 top_left = {
            this->target.x - 0.5f * this->view_width,
            this->target.y - 0.5f * view_height};
        Vector2 top_right = {top_left.x + this->view_width, top_left.y};
        Vector2 bot_right = {top_right.x, top_right.y + view_height};

        return {
            top_left.x, top_left.y, top_right.x - top_left.x, bot_right.y - top_right.y};
    }

    RectDetailed get_screen_rect_detailed() {
        return get_rect_detailed(this->get_screen_rect());
    }

    void set_shader_values(Shader shader) {
        SetShaderValue(
            shader,
            GetShaderLocation(shader, "camera.view_width"),
            &this->view_width,
            SHADER_UNIFORM_FLOAT
        );
        SetShaderValue(
            shader,
            GetShaderLocation(shader, "camera.aspect"),
            &this->aspect,
            SHADER_UNIFORM_FLOAT
        );
        SetShaderValue(
            shader,
            GetShaderLocation(shader, "camera.target"),
            &this->target,
            SHADER_UNIFORM_VEC2
        );
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
    BLOCKING,
    ATTACK_0,
    ATTACK_1,
    ATTACK_2,
    DEATH,
    DELETE,
};

enum class CreatureType {
    NONE,
    RIGID_COLLIDER,
    SPRITE,
    PLATFORM,
    PLAYER,
    BAT,
    WOLF,
    GOLEM,
};

class Light {
  public:
    float intensity = 0.0;
    Vector2 position;
    Vector3 color;
    Vector3 attenuation;

    bool is_off = true;

    // set in runtime for sorting
    float _dist;

    Light() = default;
    Light(float intensity, Vector2 position, Vector3 color, Vector3 attenuation)
        : intensity(intensity)
        , position(position)
        , color(color)
        , attenuation(attenuation)
        , is_off(false) {}
};

class Creature {
  private:
    PivotType sprite_pivot_type = PivotType::CENTER_BOTTOM;

  public:
    CreatureType type;
    CreatureState state;
    SpriteSheetAnimator animator;
    Light light;

    float move_speed = 0.0;
    float jump_speed = 0.0;
    float max_health = 0.0;
    float health = 0.0;
    float damage = 0.0;
    float attack_distance = 0.0;
    bool can_view_vertically = false;

    Vector2 position = {0.0, 0.0};
    Vector2 velocity = {0.0, 0.0};

    bool is_hflip = false;
    bool is_grounded = false;
    bool is_flying = false;
    bool can_see_player = false;
    bool can_attack_player = false;
    float landed_at_speed = 0.0;
    float last_received_damage_time = -1.0;
    std::unordered_set<uint32_t> received_attack_ids;

    // RIGID_COLLIDER
    Rectangle rigid_collider_rect;

    // PLATFORM
    std::unordered_set<Creature *> creatures_on_platform;
    std::string platform_tag;
    Vector2 platform_start;
    Vector2 platform_end;
    float platform_speed;

    Creature() {}

    Creature(
        CreatureType type,
        CreatureState state,
        SpriteSheetAnimator animator,
        Light light,
        float move_speed,
        float jump_speed,
        float max_health,
        float damage,
        float attack_distance,
        bool can_view_vertically,
        Vector2 position
    )
        : type(type)
        , state(state)
        , animator(animator)
        , light(light)
        , move_speed(move_speed)
        , jump_speed(jump_speed)
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

    static Creature create_platform(
        SpriteSheetAnimator animator,
        std::string tag,
        float speed,
        Vector2 start,
        Vector2 end
    ) {
        Creature platform;
        platform.type = CreatureType::PLATFORM;
        platform.state = CreatureState::IDLE;
        platform.animator = animator;
        platform.platform_tag = tag;
        platform.platform_speed = speed;
        platform.position = start;
        platform.platform_start = start;
        platform.platform_end = end;
        platform.is_flying = true;

        return platform;
    }

    static Creature create_rigid_collider(Rectangle rect) {
        Creature rigid_collider;
        rigid_collider.type = CreatureType::RIGID_COLLIDER;
        rigid_collider.rigid_collider_rect = rect;
        rigid_collider.is_flying = true;

        return rigid_collider;
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

    Collider get_block_collider() {
        Collider collider = this->animator.get_collider(
            "block", this->get_pivot(), is_hflip
        );
        return collider;
    }

    Rectangle get_rigid_rect() {
        Rectangle rect = {0.0, 0.0, 0.0, 0.0};
        switch (this->type) {
            case (CreatureType::RIGID_COLLIDER): rect = this->rigid_collider_rect; break;
            case (CreatureType::PLATFORM): rect = this->get_rigid_collider().mask; break;
            default: break;
        }
        return rect;
    }

    Light get_light() {
        Light light = this->light;
        light.position = Vector2Add(light.position, this->position);
        return light;
    }
};

// -----------------------------------------------------------------------
// game
class Game {
  public:
    RenderTexture2D shadow_map;

    std::unordered_map<std::string, Shader> shaders;
    std::unordered_map<std::string, SpriteSheet> sprite_sheets;
    TiledLevel tiled_level;

    std::vector<Creature> creatures;
    std::vector<Creature> new_creatures;

    GameCamera camera;

    Creature *player;
    float dt = 0.0;
    float time = 0.0;

    Game() {
        SetConfigFlags(FLAG_MSAA_4X_HINT);
        SetTargetFPS(60);
        InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Game");

        rlDisableBackfaceCulling();

        this->camera = GameCamera(500.0, (float)SCREEN_WIDTH / SCREEN_HEIGHT);
        this->shadow_map = LoadRenderTexture(SHADOW_MAP_WIDTH, SHADOW_MAP_HEIGHT);
        SetTextureWrap(this->shadow_map.texture, TEXTURE_WRAP_CLAMP);

        this->shaders["sprite"] = load_shader("base.vert", "sprite.frag");
        this->shaders["shadow"] = load_shader("base.vert", "shadow.frag");
        this->sprite_sheets["0"] = SpriteSheet("./resources/sprite_sheets/", "0");
        this->load_level(LEVELS_DIR, LEVEL);
    }

    ~Game() {
        this->tiled_level.unload();

        UnloadRenderTexture(this->shadow_map);

        for (auto &pair : this->shaders)
            UnloadShader(pair.second);
        for (auto &pair : this->sprite_sheets)
            pair.second.unload();

        CloseWindow();
    }

    void load_level(std::string dir_path, std::string name) {
        this->tiled_level.unload();
        this->creatures.clear();

        this->tiled_level = TiledLevel(dir_path, name);

        // get all objects for the map
        std::unordered_map<int, json> objects;
        for (auto layer_json : this->tiled_level.meta["layers"]) {
            for (auto object : layer_json["objects"]) {
                objects[object["id"]] = object;
            }
        }

        // iterate over all objects and create game entities
        for (auto &item : objects) {
            auto object = item.second;
            auto object_x = object["x"];
            auto object_y = object["y"];
            auto object_name = object["name"];
            auto object_width = object["width"];
            auto object_height = object["height"];
            Vector2 object_position = {.x = object_x, .y = object_y};

            std::string object_type = "";
            std::string object_tag = "";
            int destination_object_id = -1;
            for (auto property : object["properties"]) {
                std::string name = property["name"];
                auto value = property["value"];
                if (name == "type") object_type = value;
                else if (name == "destination") destination_object_id = value;
                else if (name == "tag") object_tag = value;
            }

            if (object_type == "rigid_collider") {
                this->creatures.push_back(Creature::create_rigid_collider(
                    {.x = object_x,
                     .y = object_y,
                     .width = object_width,
                     .height = object_height}
                ));
            } else if (object_type == "player") {
                this->creatures.push_back(Creature(
                    CreatureType::PLAYER,
                    CreatureState::IDLE,
                    SpriteSheetAnimator(&this->sprite_sheets["0"], "knight"),
                    Light(30.0, {0.0, -16.0}, {1.0, 0.9, 0.8}, {25.0, 0.2, 0.007}),
                    100.0,
                    250.0,
                    1000.0,
                    50.0,
                    0.0,
                    true,
                    object_position
                ));
                this->camera.target = object_position;
            } else if (object_type == "bat") {
                this->creatures.push_back(Creature(
                    CreatureType::BAT,
                    CreatureState::IDLE,
                    SpriteSheetAnimator(&this->sprite_sheets["0"], "bat"),
                    Light(),
                    50.0,
                    0.0,
                    300.0,
                    50.0,
                    25.0,
                    true,
                    object_position
                ));
            } else if (object_type == "wolf") {
                this->creatures.push_back(Creature(
                    CreatureType::WOLF,
                    CreatureState::IDLE,
                    SpriteSheetAnimator(&this->sprite_sheets["0"], "wolf"),
                    Light(),
                    80.0,
                    0.0,
                    300.0,
                    50.0,
                    35.0,
                    false,
                    object_position
                ));
            } else if (object_type == "golem") {
                this->creatures.push_back(Creature(
                    CreatureType::GOLEM,
                    CreatureState::IDLE,
                    SpriteSheetAnimator(&this->sprite_sheets["0"], "golem"),
                    Light(30.0, {0.0, -32.0}, {1.0, 0.2, 0.1}, {25.0, 0.5, 0.1}),
                    60.0,
                    0.0,
                    400.0,
                    50.0,
                    35.0,
                    false,
                    object_position
                ));
            } else if (object_type == "platform") {
                auto dest = objects[destination_object_id];
                std::string base_name = "platform_" + object_tag;
                this->creatures.push_back(Creature::create_platform(
                    SpriteSheetAnimator(&this->sprite_sheets["0"], base_name),
                    object_tag,
                    PLATFORM_SPEED,
                    object_position,
                    {.x = dest["x"], .y = dest["y"]}
                ));
            } else if (object_type == "light") {
                std::string base_name = "light_" + object_tag;
                PivotType pivot_type = PivotType::CENTER_BOTTOM;
                Light light(100.0, {0.0, 16.0}, {1.0, 0.7, 0.2}, {25.0, 0.5, 0.1});
                if (object_tag == "0") {
                    pivot_type = PivotType::CENTER_TOP;
                }
                Creature creature = Creature::create_sprite(
                    SpriteSheetAnimator(&this->sprite_sheets["0"], base_name),
                    object_position,
                    false,
                    pivot_type
                );
                creature.light = light;
                creature.animator.play(0.2, true);
                this->creatures.push_back(creature);
            }
        }
    }

    void update() {
        if (IsKeyPressed(KEY_R)) {
            this->load_level(LEVELS_DIR, LEVEL);
            return;
        }

        // find player
        for (auto &creature : this->creatures) {
            if (creature.type == CreatureType::PLAYER) this->player = &creature;
        }

        this->dt = GetFrameTime();
        this->time += this->dt;
        this->camera.target = player->position;

        for (auto &creature : this->creatures) {
            creature.animator.update(this->dt);

            // turn off light if non-player creature is dead
            if (creature.state == CreatureState::DEATH
                && creature.type != CreatureType::PLAYER) {
                creature.light.is_off = true;
            }

            // clear old received attack ids once in a while
            if (this->time - creature.last_received_damage_time > 5.0
                && creature.received_attack_ids.size()) {
                creature.received_attack_ids.clear();
            }

            // immediate position_step needs to be computed by
            // the Character update logic (will be applied later)
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
                        // -> MOVING, JUMPING, FALLING, BLOCKING, ATTACK_0
                        creature.animator.play("idle", 0.1, true);

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
                            creature.velocity.y = -creature.jump_speed;
                            creature.state = CreatureState::JUMPING;
                        } else if (IsKeyPressed(KEY_SPACE)) {
                            // -> ATTACK_0
                            creature.state = CreatureState::ATTACK_0;
                        } else if (IsKeyPressed(KEY_LEFT_SHIFT)) {
                            // -> BLOCKING
                            creature.state = CreatureState::BLOCKING;
                        } else if (position_step.x) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        }

                        break;
                    case CreatureState::MOVING:
                        // -> JUMPING, FALLING, DASHING, ATTACK_0
                        creature.animator.play("run", 0.1, true);

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
                            creature.velocity.y = -creature.jump_speed;
                            creature.state = CreatureState::JUMPING;
                        } else if (IsKeyPressed(KEY_LEFT_CONTROL)) {
                            // -> DASHING
                            creature.state = CreatureState::DASHING;
                        } else if (IsKeyPressed(KEY_LEFT_SHIFT)) {
                            // -> BLOCKING
                            creature.state = CreatureState::BLOCKING;
                        } else if (IsKeyPressed(KEY_SPACE)) {
                            // -> ATTACK_0
                            creature.state = CreatureState::ATTACK_0;
                        } else if (!position_step.x) {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        }

                        break;
                    case CreatureState::JUMPING:
                        // -> IDLE, MOVING, FALLING
                        creature.animator.play("jump", 0.1, false);

                        // move
                        if (IsKeyDown(KEY_D))
                            position_step.x += creature.move_speed * this->dt;
                        if (IsKeyDown(KEY_A))
                            position_step.x -= creature.move_speed * this->dt;

                        if (creature.velocity.y > EPSILON) {
                            // -> FALLING
                            creature.state = CreatureState::FALLING;
                        } else if (position_step.x && creature.is_grounded) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        } else if (creature.is_grounded) {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        }

                        break;
                    case CreatureState::FALLING:
                        // -> IDLE, MOVING, DASHING, LANDING
                        creature.animator.play("fall", 0.1, false);

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
                        creature.animator.play("landing", 0.1, false);

                        // -> IDLE
                        if (creature.animator.is_finished()) {
                            creature.state = CreatureState::IDLE;
                        }

                        break;
                    case CreatureState::DASHING:
                        // -> IDLE, FALLING, ATTACK_0
                        creature.animator.play("roll", 0.1, false);

                        static float attack_0_pressed_at_progress = -INFINITY;

                        // check if ATTACK_0 is pressed while DASHING
                        if (IsKeyPressed(KEY_SPACE)
                            && attack_0_pressed_at_progress == -INFINITY) {
                            attack_0_pressed_at_progress = creature.animator.progress;
                        }

                        if (!creature.animator.is_finished()) {
                            position_step.x += creature.get_view_dir()
                                               * creature.move_speed * this->dt;

                            // continue DASHING if the animation is not finished yet
                            break;
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
                    case CreatureState::BLOCKING:
                        // -> IDLE
                        creature.animator.play("block", 0.05, false);

                        if (creature.animator.is_finished()) {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        }

                        break;
                    case CreatureState::ATTACK_0:
                        // -> IDLE, FALLING, ATTACK_1
                        creature.animator.play(
                            "attack_0", ATTACK_0_FRAME_DURATION, false
                        );

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
                        creature.animator.play(
                            "attack_1", ATTACK_1_FRAME_DURATION, false
                        );

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
                        creature.animator.play(
                            "attack_2", ATTACK_2_FRAME_DURATION, false
                        );

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
                        creature.animator.play("death", 0.1, false);

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
                        creature.animator.play("flight", 0.04, true);

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
                        creature.animator.play("flight", 0.04, true);

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
                        creature.animator.play("attack", 0.1, true);

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
                        creature.animator.play("fall", 0.1, false);

                        if (creature.health <= 0.0 && creature.animator.is_finished()
                            && creature.is_grounded) {
                            creature.state = CreatureState::DEATH;
                        } else if (creature.is_flying) {
                            creature.state = CreatureState::IDLE;
                        }

                        break;
                    case CreatureState::DEATH:
                        creature.animator.play("death", 0.1, false);

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
                        creature.animator.play("idle", 0.1, true);

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
                        creature.animator.play("run", 0.1, true);

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
                        creature.animator.play("attack", 0.1, true);

                        if (!creature.can_see_player) {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        } else if (!creature.can_attack_player && creature.animator.progress < 0.3) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        }

                        break;
                    case CreatureState::DEATH:
                        creature.animator.play("death", 0.1, false);

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
                        creature.animator.play("idle", 0.1, true);

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
                        creature.animator.play("run", 0.1, true);

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
                        creature.animator.play("attack", 0.1, true);

                        if (!creature.can_see_player) {
                            // -> IDLE
                            creature.state = CreatureState::IDLE;
                        } else if (!creature.can_attack_player && creature.animator.progress < 0.3) {
                            // -> MOVING
                            creature.state = CreatureState::MOVING;
                        }

                        break;
                    case CreatureState::DEATH:
                        creature.animator.play("death", 0.1, false);

                        break;
                    default: break;
                }
            } else if (creature.type == CreatureType::SPRITE) {
                if (creature.animator.is_finished()) {
                    creature.state = CreatureState::DELETE;
                }
            } else if (creature.type == CreatureType::PLATFORM) {
                // don't care in which state the PLATFORM is, always the same logic
                creature.animator.play("idle", 0.1, true);

                // move the platform
                float speed = creature.platform_speed;
                Vector2 target = speed > 0.0 ? creature.platform_end
                                             : creature.platform_start;
                float dist = Vector2Distance(target, creature.position);
                Vector2 dir = Vector2Normalize(Vector2Subtract(target, creature.position)
                );
                Vector2 step = Vector2Scale(dir, fabs(speed) * this->dt);
                if (Vector2Length(step) >= dist) {
                    step = Vector2Scale(dir, dist);
                    creature.platform_speed *= -1;
                }
                creature.position = Vector2Add(creature.position, step);

                // move creatures on the platform
                for (Creature *other : creature.creatures_on_platform) {
                    other->position = Vector2Add(other->position, step);
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
            float mtv_neg_x = 0.0;
            float mtv_pos_x = 0.0;
            float mtv_neg_y = 0.0;
            float mtv_pos_y = 0.0;
            for (Creature &collider_creature : this->creatures) {
                if (&rigid_creature == &collider_creature) continue;

                Rectangle rect = collider_creature.get_rigid_rect();
                if (rect.width <= 0.0) continue;

                Vector2 collider_mtv = get_aabb_mtv(rigid_collider.mask, rect);

                mtv_neg_x = std::min(mtv_neg_x, collider_mtv.x);
                mtv_pos_x = std::max(mtv_pos_x, collider_mtv.x);
                mtv_neg_y = std::min(mtv_neg_y, collider_mtv.y);
                mtv_pos_y = std::max(mtv_pos_y, collider_mtv.y);

                if (collider_creature.type == CreatureType::PLATFORM) {
                    if (collider_mtv.y < 0.0) {
                        // put creature on the platform
                        collider_creature.creatures_on_platform.insert(&rigid_creature);
                    } else {
                        // remove creature from the platform
                        collider_creature.creatures_on_platform.erase(&rigid_creature);
                    }
                }
            }

            // resolve mtv
            Vector2 mtv = {mtv_neg_x, mtv_neg_y};
            if (fabs(mtv_pos_x) > fabs(mtv_neg_x)) mtv.x = mtv_pos_x;
            if (fabs(mtv_pos_y) > fabs(mtv_neg_y)) mtv.y = mtv_pos_y;

            // vertical smash
            if (fabs(mtv_pos_y) > EPSILON && fabs(mtv_neg_y) > EPSILON) {
                // when smashed vertically, set mtv.y to the negative one,
                // e.g resolve only floor collision
                mtv.y = mtv_neg_y;
                this->receive_damage(rigid_creature, rigid_creature.health);
            }
            // horizontal smash
            if (fabs(mtv_pos_x) > EPSILON && fabs(mtv_neg_x) > EPSILON) {
                mtv.x = 0.0;
                this->receive_damage(rigid_creature, rigid_creature.health);
            }

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
                Collider attack_collider = attacker_creature.get_attack_collider();
                Collider block_collider = rigid_creature.get_block_collider();

                if (!attack_collider.id) continue;

                // creature can't attack itself
                if (&attacker_creature == &rigid_creature) continue;

                // creature is already dead
                if (rigid_creature.health <= 0.0) continue;

                // one of the creatures must be the PLAYER
                if (rigid_creature.type != CreatureType::PLAYER
                    && attacker_creature.type != CreatureType::PLAYER)
                    continue;

                // ignore already received attack
                if (rigid_creature.received_attack_ids.find(attack_collider.id)
                    != rigid_creature.received_attack_ids.end()) {
                    continue;
                }

                if (block_collider.id
                    && CheckCollisionRecs(attack_collider.mask, block_collider.mask)) {
                    // if block is successful, apply damage to the attacker
                    std::string base_name = "block_effect_" + std::to_string(rand() % 3);
                    Creature block = Creature::create_sprite(
                        SpriteSheetAnimator(&this->sprite_sheets["0"], base_name),
                        get_rect_center(block_collider.mask),
                        rigid_creature.is_hflip,
                        PivotType::CENTER_CENTER
                    );
                    block.animator.play(0.02, false);
                    this->new_creatures.push_back(block);

                    rigid_creature.received_attack_ids.insert(attack_collider.id);
                    attacker_creature.received_attack_ids.insert(attack_collider.id);

                    this->receive_damage(attacker_creature, rigid_creature.damage);
                    attacker_creature.velocity = {
                        .x = rigid_creature.get_view_dir() * 75.0f, .y = -75.0f};
                } else if (CheckCollisionRecs(
                               rigid_collider.mask, attack_collider.mask
                           )) {
                    // else if attack is successful, apply damage to the target
                    rigid_creature.received_attack_ids.insert(attack_collider.id);

                    this->receive_damage(rigid_creature, attacker_creature.damage);
                    rigid_creature.velocity = {
                        .x = attacker_creature.get_view_dir() * 75.0f, .y = -75.0f};
                }
            }
        }

        // -----------------------------------------------------------
        // update can_see_player, can_attack_player
        for (Creature &creature : this->creatures) {
            creature.can_see_player = false;
            creature.can_attack_player = false;

            // can't see and can't attack the dead player or if dead itself
            if (this->player->health <= 0.0) continue;
            if (creature.health <= 0.0) continue;

            Vector2 view_line_start = creature.position;
            Vector2 view_line_end = player->position;
            float dist = Vector2Distance(view_line_start, view_line_end);

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
            for (Creature &collider_creature : this->creatures) {
                if (&creature == &collider_creature) continue;

                Rectangle rect = collider_creature.get_rigid_rect();
                if (rect.width <= 0.0) continue;

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

        // -----------------------------------------------------------
        // push new creatures
        this->creatures.insert(
            this->creatures.end(), this->new_creatures.begin(), this->new_creatures.end()
        );
        this->new_creatures.clear();
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
        // draw scene
        this->draw_shadow_map();
        this->update_lights();

        BeginDrawing();
        rlViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        ClearBackground(BLANK);

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

        // healthbar
        float health_ratio = this->player->health / this->player->max_health;
        int max_bar_width = 300;
        int bar_width = health_ratio * max_bar_width;
        DrawRectangle(5, 5, bar_width, 30, RED);

        EndDrawing();
    }

    Vector2 get_step_towards_player(Creature &creature) {
        if (&creature == this->player) return {0.0, 0.0};

        Vector2 start = creature.position;
        Vector2 end = player->position;
        Vector2 dir = Vector2Normalize(Vector2Subtract(end, start));
        Vector2 step = Vector2Scale(dir, creature.move_speed * this->dt);

        return step;
    }

    void receive_damage(Creature &creature, float damage) {
        if (creature.health <= 0.0) return;

        creature.health -= damage;
        creature.last_received_damage_time = this->time;
    }

    void update_lights() {
        // ---------------------------------------------------------------
        // collect lights
        Shader sprite_shader = this->shaders["sprite"];

        static std::vector<Light> lights;
        lights.clear();

        for (Creature &creature : this->creatures) {
            if (!creature.light.is_off) {
                Light light = creature.get_light();
                light._dist = Vector2Distance(creature.position, this->camera.target);
                lights.push_back(light);
            }
        }

        std::sort(
            lights.begin(),
            lights.end(),
            [](const Light &light1, const Light &light2) {
                return light1._dist < light2._dist;
            }
        );
        if (lights.size() > MAX_N_LIGHTS) {
            lights.resize(MAX_N_LIGHTS);
        }

        // ---------------------------------------------------------------
        // set shader values
        int n_lights = lights.size();
        SetShaderValue(
            sprite_shader,
            GetShaderLocation(sprite_shader, "n_lights"),
            &n_lights,
            SHADER_UNIFORM_INT
        );

        for (int i = 0; i < n_lights; ++i) {
            Light &light = lights[i];
            std::string light_name = "lights[" + std::to_string(i) + "]";

            SetShaderValue(
                sprite_shader,
                GetShaderLocation(sprite_shader, (light_name + ".intensity").c_str()),
                &light.intensity,
                SHADER_UNIFORM_FLOAT
            );
            SetShaderValue(
                sprite_shader,
                GetShaderLocation(sprite_shader, (light_name + ".position").c_str()),
                &light.position,
                SHADER_UNIFORM_VEC2
            );
            SetShaderValue(
                sprite_shader,
                GetShaderLocation(sprite_shader, (light_name + ".color").c_str()),
                &light.color,
                SHADER_UNIFORM_VEC3
            );
            SetShaderValue(
                sprite_shader,
                GetShaderLocation(sprite_shader, (light_name + ".attenuation").c_str()),
                &light.attenuation,
                SHADER_UNIFORM_VEC3
            );
        }
    }

    void draw_shadow_map() {
        static std::vector<Triangle> triangles;
        triangles.clear();

        RectDetailed screen = this->camera.get_screen_rect_detailed();
        float diag = Vector2Distance(screen.lt, screen.rb);

        // TODO: create and use Eyes component instead of light for this
        Light light = this->player->get_light();

        // push shadow triangles
        for (Creature &obstacle : this->creatures) {
            Rectangle rect = obstacle.get_rigid_rect();
            if (rect.width <= 0.0) continue;

            RectDetailed obst = get_rect_detailed(rect);

            // get shadow walls
            Line walls[4];
            int n_walls = 0;
            if (light.position.x < obst.right.a.x) walls[n_walls++] = obst.right;
            if (light.position.x > obst.left.a.x) walls[n_walls++] = obst.left;
            if (light.position.y < obst.bot.a.y) walls[n_walls++] = obst.bot;
            if (light.position.y > obst.top.a.y) walls[n_walls++] = obst.top;

            // get shadow triangles
            for (int i = 0; i < n_walls; ++i) {
                Line start = walls[i];
                Line dir = {
                    Vector2Normalize(Vector2Subtract(start.a, light.position)),
                    Vector2Normalize(Vector2Subtract(start.b, light.position))};
                Line end = {
                    Vector2Add(start.a, Vector2Scale(dir.a, diag)),
                    Vector2Add(start.b, Vector2Scale(dir.b, diag))};
                Line side0 = {start.a, end.a};
                Line side1 = {start.b, end.b};

                Vector2 point;
                int intersection = 0;
                for (Line side : {side0, side1}) {
                    if (check_collision_lines(side, screen.left)) intersection |= LEFT;
                    if (check_collision_lines(side, screen.top)) intersection |= TOP;
                    if (check_collision_lines(side, screen.right)) intersection |= RIGHT;
                    if (check_collision_lines(side, screen.bot)) intersection |= BOT;
                }

                switch (intersection) {
                    case LEFT:
                    case TOP:
                    case RIGHT:
                    case BOT:
                        triangles.push_back({start.a, end.a, start.b});
                        triangles.push_back({start.b, end.a, end.b});
                        break;
                    case LEFT | BOT:
                        triangles.push_back({start.a, end.a, screen.lb});
                        triangles.push_back({start.b, end.b, screen.lb});
                        triangles.push_back({start.a, start.b, screen.lb});
                        break;
                    case LEFT | TOP:
                        triangles.push_back({start.a, end.a, screen.lt});
                        triangles.push_back({start.b, end.b, screen.lt});
                        triangles.push_back({start.a, start.b, screen.lt});
                        break;
                    case RIGHT | TOP:
                        triangles.push_back({start.a, end.a, screen.rt});
                        triangles.push_back({start.b, end.b, screen.rt});
                        triangles.push_back({start.a, start.b, screen.rt});
                        break;
                    case RIGHT | BOT:
                        triangles.push_back({start.a, end.a, screen.rb});
                        triangles.push_back({start.b, end.b, screen.rb});
                        triangles.push_back({start.a, start.b, screen.rb});
                        break;
                    case LEFT | RIGHT:
                        if (light.position.y < start.a.y) {
                            triangles.push_back({start.a, end.a, screen.lb});
                            triangles.push_back({start.a, start.b, screen.lb});
                            triangles.push_back({start.b, screen.rb, screen.lb});
                            triangles.push_back({start.b, end.b, screen.rb});
                        } else {
                            triangles.push_back({start.a, end.a, screen.lt});
                            triangles.push_back({start.a, start.b, screen.lt});
                            triangles.push_back({start.b, screen.rt, screen.lt});
                            triangles.push_back({start.b, end.b, screen.rt});
                        }
                        break;
                    case TOP | BOT:
                        if (light.position.x < start.a.x) {
                            triangles.push_back({start.a, end.a, screen.rt});
                            triangles.push_back({start.a, start.b, screen.rt});
                            triangles.push_back({start.b, screen.rb, screen.rt});
                            triangles.push_back({start.b, end.b, screen.rb});
                        } else {
                            triangles.push_back({start.a, end.a, screen.lt});
                            triangles.push_back({start.a, start.b, screen.lt});
                            triangles.push_back({start.b, screen.lb, screen.lt});
                            triangles.push_back({start.b, end.b, screen.lb});
                        }
                        break;
                }
            }
        }

        // draw shadow triangles into the shadow map
        Shader shader = this->shaders["shadow"];
        BeginShaderMode(shader);
        BeginTextureMode(this->shadow_map);
        rlViewport(0, 0, SHADOW_MAP_WIDTH, SHADOW_MAP_HEIGHT);
        ClearBackground(BLANK);

        this->camera.set_shader_values(shader);
        for (Triangle &triangle : triangles) {
            DrawTriangle(triangle.a, triangle.b, triangle.c, WHITE);
        }

        EndTextureMode();
        EndShaderMode();
    }

    void draw_sprites(std::vector<Sprite> sprites, Color plain_color) {
        Shader shader = this->shaders["sprite"];

        BeginShaderMode(shader);

        this->camera.set_shader_values(shader);

        Vector4 color = ColorNormalize(plain_color);
        SetShaderValue(
            shader, GetShaderLocation(shader, "plain_color"), &color, SHADER_UNIFORM_VEC4
        );
        SetShaderValueTexture(
            shader, GetShaderLocation(shader, "shadow_map"), this->shadow_map.texture
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
