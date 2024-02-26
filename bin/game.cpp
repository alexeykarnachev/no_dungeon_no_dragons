#include "../src/renderer.hpp"

int main(void) {
    Renderer renderer;
    World world;

    while (!WindowShouldClose()) {
        renderer.draw_world(&world);
    }

    return 0;
}
