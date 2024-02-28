#include "world.hpp"

World::World() : field(), camera() {
}

void World::update() {
    camera.update_orbital();
}
