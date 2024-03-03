#include "raylib.h"
#include "raymath.h"
#include "rcamera.h"
#include "rlgl.h"
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

class ThirdPersonCamera {
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
    }
};

class ThirdPersonController {
  public:
    ThirdPersonController(){};

    Vector3 get_step(float dt, Camera3D camera3d, Vector3 position) {
        Vector2 dir = Vector2Zero();
        if (IsKeyDown(KEY_W)) dir.y -= 1.0;
        if (IsKeyDown(KEY_S)) dir.y += 1.0;
        if (IsKeyDown(KEY_A)) dir.x -= 1.0;
        if (IsKeyDown(KEY_D)) dir.x += 1.0;

        if (dir.x != 0.0 || dir.y != 0.0) {
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

                Vector3 d = Vector3Subtract(isect, position);
                d = Vector3Normalize(d);
                Vector3 step = Vector3Scale(d, dt * 1.5);
                return step;
            }
        }

        return Vector3Zero();
    }
};

class Player {
  public:
    ThirdPersonCamera camera;
    ThirdPersonController controller;
    Transform transform;

    Player(Vector3 position) : camera(position) {
        transform.rotation = QuaternionIdentity();
        transform.scale = Vector3One();
        transform.translation = position;
    }

    void update(float dt) {
        Vector3 step = controller.get_step(dt, camera.camera3d, transform.translation);
        transform.translation = Vector3Add(transform.translation, step);

        camera.update(transform.translation);
    }
};

class World {
  public:
    Player player;

    World(Vector3 player_position) : player(player_position) {}

    void update() {
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
        DrawModel(
            resources.models["player"], world.player.transform.translation, 0.01, WHITE
        );
        DrawGrid(16, 1.0);
        EndMode3D();
        DrawFPS(0, 0);
    }
};

int main(void) {
    Renderer renderer;
    World world(Vector3Zero());
    Resources resources;

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(DARKBLUE);

        world.update();
        renderer.draw_world(world, resources);

        EndDrawing();
    }

    return 0;
}
