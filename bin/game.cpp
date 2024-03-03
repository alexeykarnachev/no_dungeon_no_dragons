#include "raylib.h"
#include "raymath.h"
#include "rcamera.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;

#define print_vec3(v) (printf("%f, %f, %f\n", v.x, v.y, v.z))
#define print_vec2(v) (printf("%f, %f\n", v.x, v.y))

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

class Resources {
  public:
    std::unordered_map<std::string, Shader> shaders;
    std::unordered_map<std::string, Model> models;

    Resources() {
        models["player"] = LoadModel("resources/models/zed_1.glb");
    }

    ~Resources() {
        for (auto &pair : shaders)
            UnloadShader(pair.second);
        for (auto &pair : models)
            UnloadModel(pair.second);
    }
};

class GameCamera {
  public:
    Camera3D camera3d;

    GameCamera(Vector3 target) {
        camera3d.target = target;
        camera3d.position = (Vector3){0.0, 5.0, -5.0};
        camera3d.fovy = 70.0f;
        camera3d.projection = CAMERA_PERSPECTIVE;
        camera3d.up = {0.0f, 1.0f, 0.0f};
    }

    void update_third_person(Vector3 target) {
        static float rot_speed = 0.003f;

        Vector3 translation = Vector3Subtract(target, camera3d.target);
        camera3d.position = Vector3Add(translation, camera3d.position);
        camera3d.target = target;

        Vector2 mouse_delta = GetMouseDelta();
        CameraYaw(&camera3d, -rot_speed * mouse_delta.x, true);
        CameraPitch(&camera3d, rot_speed * mouse_delta.y, true, true, false);
    }
};

class Player {
  public:
    GameCamera camera;
    Transform transform;

    Player(Vector3 position) : camera(position) {
        transform.rotation = QuaternionIdentity();
        transform.scale = Vector3One();
        transform.translation = position;
    }

    void update() {
        camera.update_third_person(transform.translation);
    }
};

class World {
  public:
    Player player;

    World(Vector3 player_position) : player(player_position) {}

    void update() {
        player.update();
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
        BeginDrawing();
        ClearBackground(DARKBLUE);

        BeginMode3D(world.player.camera.camera3d);
        DrawModel(
            resources.models["player"], world.player.transform.translation, 0.01, WHITE
        );
        DrawGrid(16, 1.0);
        EndMode3D();
        DrawFPS(0, 0);

        EndDrawing();
    }
};

int main(void) {
    Renderer renderer;
    World world(Vector3Zero());
    Resources resources;

    while (!WindowShouldClose()) {
        world.update();
        renderer.draw_world(world, resources);
    }

    return 0;
}
