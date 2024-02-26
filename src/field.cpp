#include "field.hpp"
#include <algorithm>

Field::Field() {
    std::fill(std::begin(cell_types), std::end(cell_types), CellType::EMPTY);
}

u32 Field::get_n_cells() {
    return N_CELLS;
}

Vector2 Field::get_cell_xy(u32 id) {
    u32 row = HEIGHT - 1 - id / WIDTH;
    u32 col = id % WIDTH;
    float x = col * CELL_SIZE;
    float y = row * CELL_SIZE;

    return {x, y};
}
