#pragma once

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
