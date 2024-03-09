#include "json.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
using json = nlohmann::json;

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
    SpriteSheet(std::string meta_file_path, std::string texture_file_path) {
        std::ifstream file(meta_file_path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + meta_file_path);
        }

        meta = json::parse(file);
        file.close();

        texture = LoadTexture(texture_file_path.c_str());
    }

    ~SpriteSheet() {
        UnloadTexture(texture);
    }

    int count_frames(std::string &name) {
        return meta["frames"][name].size();
    }

    Sprite get_sprite(std::string &name, int idx) {
        json frame_json = meta["frames"][name][idx];
        Sprite sprite(frame_json, this->texture);
        return sprite;
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
        int n_frames = this->sprite_sheet.count_frames(this->name);
        int idx = std::round(this->progress * (n_frames - 1.0));
        Sprite sprite = this->sprite_sheet.get_sprite(this->name, idx);
        return sprite;
    }
};

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    SetTargetFPS(60);
    InitWindow(1024, 768, "Game");

    SpriteSheet sheet("./resources/sprites/atlas.json", "./resources/sprites/atlas.png");

    SpriteSheetAnimator animator(sheet);
    animator.play("knight_attack_0", 0.1, true);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BLACK);

        animator.update(GetFrameTime());
        Sprite sprite = animator.get_sprite();

        EndDrawing();
    }
}
