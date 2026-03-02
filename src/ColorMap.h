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
    Spectral,      // Diverging: red → orange → yellow → green → blue
    PiYG,          // Diverging: pink → white → yellow-green
    Cubehelix,     // Monotonic luminance with color helix
    YlOrRd,        // Sequential: yellow → orange → red
    Jet,           // Classic rainbow (non-perceptual)
    Tab10,         // 10 distinct categorical colors
    COUNT
};

const char* ColorMapName(ColorMapType type);
std::vector<std::string> AllColorMapNames();

// Map a value in [0, 1] to RGB through the given colormap
void ColorMapLookup(ColorMapType type, float t, float& r, float& g, float& b,
                    bool reversed = false);
