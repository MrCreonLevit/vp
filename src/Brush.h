// Viewpoints (MIT License) - See LICENSE file
#pragma once

#include <array>

constexpr int NUM_BRUSHES = 7;

struct BrushDef {
    float r, g, b;
    const char* name;
};

// Default brush colors matching the original Viewpoints
constexpr std::array<BrushDef, NUM_BRUSHES> kDefaultBrushes = {{
    {1.0f, 0.2f, 0.2f, "Red"},
    {0.2f, 0.4f, 1.0f, "Blue"},
    {0.2f, 0.9f, 0.2f, "Green"},
    {1.0f, 0.2f, 1.0f, "Magenta"},
    {0.2f, 1.0f, 1.0f, "Cyan"},
    {1.0f, 1.0f, 0.2f, "Yellow"},
    {0.6f, 0.6f, 0.6f, "Grey"},
}};
