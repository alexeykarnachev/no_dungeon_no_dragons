#include "raylib.h"
#include "raymath.h"
#include "rcamera.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;

#define print_vec3(v) (printf("%f, %f, %f\n", v.x, v.y, v.z))
#define print_vec2(v) (printf("%f, %f\n", v.x, v.y))

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

class AnimatedModel {
  public:
    Model model;
    ModelAnimation *animations;

    int animation_idx = 0;
    int n_animations = 0;
    int n_frames = 0;
    int fps = 0;
    int frame_idx = 0;
    float time = 0.0;

    AnimatedModel() {}

    AnimatedModel(const char *file_path) {
        model = LoadModel(file_path);
        animations = LoadModelAnimations(file_path, &n_animations);
        if (n_animations == 0) return;

        n_frames = animations[0].frameCount;
        fps = 30;
    }

    void play(std::string animation_name, int fps, float dt) {
        this->fps = fps;

        int animation_idx = -1;
        for (int i = 0; i < n_animations; ++i) {
            if (animation_name == animations[i].name) {
                animation_idx = i;
            }
        }

        if (animation_idx == -1) {
            throw std::runtime_error("Animation " + animation_name + " is not found");
        }

        if (this->animation_idx != animation_idx) {
            this->animation_idx = animation_idx;
            n_frames = animations[this->animation_idx].frameCount;
            time = 0.0;
        }

        if (n_frames <= 0) {
            throw std::runtime_error(
                "Can't update the AnimationCounter with n_frames <= 0"
            );
        }

        time += dt;
        frame_idx = time / (1.0f / fps);
        frame_idx %= n_frames;
    }

    void draw() {
        UpdateModelAnimation(model, animations[animation_idx], frame_idx);
        DrawModel(model, Vector3Zero(), 0.01, WHITE);
    }
};

class Resources {
  public:
    std::unordered_map<std::string, Shader> shaders;
    std::unordered_map<std::string, AnimatedModel> animated_models;

    Resources() {
        animated_models["zed_1"] = AnimatedModel("resources/models/zed_1.glb");
    }

    ~Resources() {
        for (auto &pair : shaders)
            UnloadShader(pair.second);
        for (auto &pair : animated_models)
            UnloadModelAnimations(pair.second.animations, pair.second.n_animations);
    }
};

class ThirdPersonCamera {
  private:
    float min_y = 0.2;
    float dist_to_target = 5.0;

  public:
    Camera3D camera3d;

    ThirdPersonCamera(Vector3 target) {
        camera3d.target = target;
        camera3d.position = (Vector3){0.0, 3.0, -3.0};
        camera3d.fovy = 70.0f;
        camera3d.projection = CAMERA_PERSPECTIVE;
        camera3d.up = {0.0f, 1.0f, 0.0f};
    }

    void update(Vector3 target) {
        static float rot_speed = 0.003f;

        Vector3 translation = Vector3Subtract(target, camera3d.target);
        camera3d.position = Vector3Add(translation, camera3d.position);
        camera3d.target = target;

        Vector2 mouse_delta = GetMouseDelta();
        CameraYaw(&camera3d, -rot_speed * mouse_delta.x, true);
        CameraPitch(&camera3d, -rot_speed * mouse_delta.y, true, true, false);

        camera3d.position.y = std::max(camera3d.position.y, min_y);
        CameraMoveToTarget(
            &camera3d, dist_to_target - Vector3Distance(target, camera3d.position)
        );
    }
};

class ThirdPersonController {
  private:
    Vector3 world_dir;
    u8 keys_mask;
    bool preserve_direction = false;

  public:
    ThirdPersonController()
        : keys_mask(0){};

    Vector3 get_direction(Camera3D camera3d, Vector3 position) {
        u8 new_keys_mask = 0;
        new_keys_mask |= IsKeyDown(KEY_W) << 0;
        new_keys_mask |= IsKeyDown(KEY_S) << 1;
        new_keys_mask |= IsKeyDown(KEY_A) << 2;
        new_keys_mask |= IsKeyDown(KEY_D) << 3;

        if (!new_keys_mask) {
            world_dir = Vector3Zero();
        } else if (new_keys_mask && (new_keys_mask != keys_mask || !preserve_direction)) {
            Vector2 dir = Vector2Zero();
            if (IsKeyDown(KEY_W)) dir.y -= 1.0;
            if (IsKeyDown(KEY_S)) dir.y += 1.0;
            if (IsKeyDown(KEY_A)) dir.x -= 1.0;
            if (IsKeyDown(KEY_D)) dir.x += 1.0;

            Vector2 s = GetWorldToScreen(position, camera3d);
            s = Vector2Add(s, dir);
            Ray r = GetMouseRay(s, camera3d);

            // Intersect line and plane
            Vector3 plane_normal = {0.0, 1.0, 0.0};
            float dot = Vector3DotProduct(r.direction, plane_normal);
            if (fabs(dot) > EPSILON) {
                Vector3 w = Vector3Subtract(position, r.position);
                float t = Vector3DotProduct(plane_normal, w) / dot;
                Vector3 isect = Vector3Add(
                    r.position, Vector3Scale(Vector3Normalize(r.direction), t)
                );

                world_dir = Vector3Normalize(Vector3Subtract(isect, position));
                world_dir.y = 0.0;
            }
        }

        keys_mask = new_keys_mask;
        return world_dir;
    }
};

enum class PlayerState {
    IDLE,
    WALK,
};

class Player {
  public:
    ThirdPersonCamera camera;
    ThirdPersonController controller;
    Transform transform;
    PlayerState state;
    AnimatedModel animated_model;

    Player(Vector3 position, AnimatedModel animated_model)
        : camera(position)
        , animated_model(animated_model) {
        transform.rotation = QuaternionIdentity();
        transform.scale = Vector3One();
        transform.translation = position;
        state = PlayerState::IDLE;
    }

    void update(float dt) {
        state = PlayerState::IDLE;

        Vector3 dir = controller.get_direction(camera.camera3d, transform.translation);
        if (Vector3Length(dir) > EPSILON) {
            transform.translation = Vector3Add(
                transform.translation, Vector3Scale(dir, dt * 1.0)
            );
            transform.rotation = QuaternionFromVector3ToVector3(
                (Vector3){0.0, 0.0, 1.0}, Vector3Normalize(dir)
            );

            state = PlayerState::WALK;
        }

        camera.update(transform.translation);

        if (state == PlayerState::IDLE) {
            animated_model.play("idle", 120, dt);
        } else if (state == PlayerState::WALK) {
            animated_model.play("walk", 120, dt);
        }
    }
};

class World {
  public:
    Player player;

    World(Resources *resources, Vector3 player_position)
        : player(player_position, resources->animated_models["zed_1"]) {}

    void update() {
        if (GetTime() < 0.5) return;
        player.update(GetFrameTime());
    }
};

class Renderer {
  public:
    Renderer() {
        SetConfigFlags(FLAG_MSAA_4X_HINT);
        SetTargetFPS(60);
        InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Game");
        DisableCursor();
    }

    ~Renderer() {
        CloseWindow();
    }

    void draw_world(World &world, Resources &resources) {
        BeginMode3D(world.player.camera.camera3d);

        Matrix r = QuaternionToMatrix(world.player.transform.rotation);
        Matrix t = MatrixTranslate(
            world.player.transform.translation.x,
            world.player.transform.translation.y,
            world.player.transform.translation.z
        );
        Matrix transform = MatrixMultiply(r, t);
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(transform));

        world.player.animated_model.draw();

        rlPopMatrix();

        DrawGrid(16, 2.0);
        EndMode3D();
        DrawFPS(0, 0);
    }
};

int main(void) {
    Renderer renderer;
    Resources resources;
    World world(&resources, Vector3Zero());

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BLACK);

        world.update();
        renderer.draw_world(world, resources);

        EndDrawing();
    }

    return 0;
}