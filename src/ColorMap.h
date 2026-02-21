// Viewpoints (MIT License) - See LICENSE file
#pragma once

#include <string>
#include <vector>
#include <cmath>

enum class ColorMapType {
    Default = 0,   // Single color (blue)
    Viridis,       // Perceptual: dark purple → teal → yellow
    Plasma,        // Perceptual: purple → pink → orange → yellow
    Inferno,       // Perceptual: black → purple → red → yellow
    Turbo,         // Rainbow-like: blue → cyan → green → yellow → red
    Grayscale,     // Black → white
    Hot,           // Black → red → yellow → white
    Cool,          // Cyan → magenta
    BlueRed,       // Blue → white → red (diverging)
    COUNT
};

const char* ColorMapName(ColorMapType type);
std::vector<std::string> AllColorMapNames();

// Map a value in [0, 1] to RGB through the given colormap
void ColorMapLookup(ColorMapType type, float t, float& r, float& g, float& b);
