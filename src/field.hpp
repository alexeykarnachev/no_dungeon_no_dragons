#pragma once

#include "common.hpp"
#include "raylib.h"
#include <array>

enum class CellType : u8 {
    EMPTY = 1 << 0,
    OBSTACLE = 1 << 1,
};

class Field {
private:
    static constexpr u32 WIDTH = 64;
    static constexpr u32 HEIGHT = 64;
    static constexpr u32 N_CELLS = WIDTH * HEIGHT;
    static constexpr float CELL_SIZE = 1.0;

public:
    std::array<CellType, N_CELLS> cell_types;

    Field();
    u32 get_n_cells();
    Vector2 get_cell_center(u32 id);
};
