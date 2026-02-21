// Viewpoints (MIT License) - See LICENSE file
#include "ColorMap.h"
#include <algorithm>

const char* ColorMapName(ColorMapType type) {
    switch (type) {
        case ColorMapType::Default:   return "Default (Blue)";
        case ColorMapType::Viridis:   return "Viridis";
        case ColorMapType::Plasma:    return "Plasma";
        case ColorMapType::Inferno:   return "Inferno";
        case ColorMapType::Turbo:     return "Turbo";
        case ColorMapType::Grayscale: return "Grayscale";
        case ColorMapType::Hot:       return "Hot";
        case ColorMapType::Cool:      return "Cool";
        case ColorMapType::BlueRed:   return "Blue-Red";
        default:                      return "Default";
    }
}

std::vector<std::string> AllColorMapNames() {
    std::vector<std::string> names;
    for (int i = 0; i < static_cast<int>(ColorMapType::COUNT); i++)
        names.push_back(ColorMapName(static_cast<ColorMapType>(i)));
    return names;
}

// Linear interpolation between two colors
static void lerp3(float t, float r0, float g0, float b0,
                            float r1, float g1, float b1,
                            float& r, float& g, float& b) {
    r = r0 + t * (r1 - r0);
    g = g0 + t * (g1 - g0);
    b = b0 + t * (b1 - b0);
}

// Multi-stop color ramp
struct ColorStop { float pos; float r, g, b; };

static void rampLookup(const ColorStop* stops, int n, float t,
                        float& r, float& g, float& b) {
    t = std::max(0.0f, std::min(1.0f, t));
    if (t <= stops[0].pos) { r = stops[0].r; g = stops[0].g; b = stops[0].b; return; }
    if (t >= stops[n-1].pos) { r = stops[n-1].r; g = stops[n-1].g; b = stops[n-1].b; return; }
    for (int i = 0; i < n - 1; i++) {
        if (t >= stops[i].pos && t <= stops[i+1].pos) {
            float f = (t - stops[i].pos) / (stops[i+1].pos - stops[i].pos);
            lerp3(f, stops[i].r, stops[i].g, stops[i].b,
                     stops[i+1].r, stops[i+1].g, stops[i+1].b, r, g, b);
            return;
        }
    }
    r = stops[n-1].r; g = stops[n-1].g; b = stops[n-1].b;
}

void ColorMapLookup(ColorMapType type, float t, float& r, float& g, float& b) {
    t = std::max(0.0f, std::min(1.0f, t));

    switch (type) {
        case ColorMapType::Default:
            r = 0.15f; g = 0.4f; b = 1.0f;
            break;

        case ColorMapType::Viridis: {
            static const ColorStop stops[] = {
                {0.0f, 0.267f, 0.004f, 0.329f},
                {0.25f, 0.282f, 0.140f, 0.458f},
                {0.5f, 0.127f, 0.566f, 0.551f},
                {0.75f, 0.544f, 0.774f, 0.247f},
                {1.0f, 0.993f, 0.906f, 0.144f},
            };
            rampLookup(stops, 5, t, r, g, b);
            break;
        }
        case ColorMapType::Plasma: {
            static const ColorStop stops[] = {
                {0.0f, 0.050f, 0.030f, 0.528f},
                {0.25f, 0.494f, 0.012f, 0.658f},
                {0.5f, 0.798f, 0.195f, 0.482f},
                {0.75f, 0.973f, 0.504f, 0.212f},
                {1.0f, 0.940f, 0.975f, 0.131f},
            };
            rampLookup(stops, 5, t, r, g, b);
            break;
        }
        case ColorMapType::Inferno: {
            static const ColorStop stops[] = {
                {0.0f, 0.001f, 0.000f, 0.014f},
                {0.25f, 0.341f, 0.062f, 0.429f},
                {0.5f, 0.735f, 0.215f, 0.330f},
                {0.75f, 0.978f, 0.557f, 0.035f},
                {1.0f, 0.988f, 1.000f, 0.644f},
            };
            rampLookup(stops, 5, t, r, g, b);
            break;
        }
        case ColorMapType::Turbo: {
            static const ColorStop stops[] = {
                {0.0f, 0.190f, 0.072f, 0.232f},
                {0.167f, 0.087f, 0.398f, 0.853f},
                {0.333f, 0.133f, 0.738f, 0.657f},
                {0.5f, 0.527f, 0.921f, 0.217f},
                {0.667f, 0.895f, 0.773f, 0.058f},
                {0.833f, 0.995f, 0.423f, 0.068f},
                {1.0f, 0.602f, 0.042f, 0.044f},
            };
            rampLookup(stops, 7, t, r, g, b);
            break;
        }
        case ColorMapType::Grayscale:
            r = g = b = t;
            break;

        case ColorMapType::Hot: {
            static const ColorStop stops[] = {
                {0.0f, 0.0f, 0.0f, 0.0f},
                {0.33f, 1.0f, 0.0f, 0.0f},
                {0.66f, 1.0f, 1.0f, 0.0f},
                {1.0f, 1.0f, 1.0f, 1.0f},
            };
            rampLookup(stops, 4, t, r, g, b);
            break;
        }
        case ColorMapType::Cool:
            r = t; g = 1.0f - t; b = 1.0f;
            break;

        case ColorMapType::BlueRed: {
            static const ColorStop stops[] = {
                {0.0f, 0.0f, 0.2f, 1.0f},
                {0.5f, 0.9f, 0.9f, 0.9f},
                {1.0f, 1.0f, 0.1f, 0.0f},
            };
            rampLookup(stops, 3, t, r, g, b);
            break;
        }
        default:
            r = 0.15f; g = 0.4f; b = 1.0f;
            break;
    }
}
