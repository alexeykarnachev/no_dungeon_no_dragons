#include "renderer.hpp"

Renderer::Renderer() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    SetTargetFPS(60);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Game");

    unit_cube = GenMeshCube(0.5, 0.5, 0.5);
    default_material = LoadMaterialDefault();
}

void Renderer::draw_field(Field *field) {
    for (u32 cell_id = 0; cell_id < field->get_n_cells(); ++cell_id) {
        Vector2 cell_xy = field->get_cell_xy(cell_id);
        // Matrix transform = MatrixTranslate(cell_xy.x, cell_xy.y, 0.0);
        // DrawMesh(unit_cube, default_material, transform);
    }
}

void Renderer::draw_world(World *world) {
    BeginDrawing();
    ClearBackground(DARKGRAY);

    BeginMode3D(world->camera.cam);
    draw_field(&world->field);
    EndMode3D();

    DrawFPS(0, 0);

    EndDrawing();
}

Renderer::~Renderer() {
    UnloadMesh(unit_cube);
    UnloadMaterial(default_material);
    CloseWindow();
}
