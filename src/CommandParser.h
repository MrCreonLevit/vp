// Viewpoints (MIT License) - See LICENSE file
#pragma once

#include <optional>
#include <string>

// A sentence the action log could have printed, plus a few aliases
// (case, an optional pasted timestamp, plot omitted, "(all plots)" / "all plots").
enum class CommandKind {
    Ignore,                 // a pasted rejection; do nothing
    GridLines,
    ShowUnselected,
    Histograms,
    HoverDetails,
    DeferRedraws,
    Axis,
    AxisLock,
    Normalization,
    ZAxis,
    RandomizeAxes,
    Rotate,
    ResetRotation,
    SpinRock,
    PointSize,
    Opacity,
    HistBins,
    Colormap,
    AdditiveBlending,
    Background,
    ActivePlot,
    AllPlotsHighlighted,
    GridResize,
    ActiveBrush,
    BrushColor,
    BrushSymbol,
    BrushSizeOffset,
    BrushOpacityOffset,
    BrushReset,
    ClearSelection,
    InvertSelection,
};

enum class CommandScope { Unspecified, Plot, All };

struct Command {
    CommandKind kind = CommandKind::Ignore;
    CommandScope scope = CommandScope::Unspecified;
    int row = 0;            // 1-based when scope == Plot
    int col = 0;
    bool on = false;
    int axis = 0;           // 0 = Y, 1 = X, 2 = Z
    bool hasX = false;
    bool hasY = false;
    bool spinning = false;
    bool rocking = false;
    bool reversed = false;
    bool unselectedBrush = false;
    bool zNone = false;
    int intVal = 0;
    float f0 = 0.f, f1 = 0.f, f2 = 0.f, f3 = 0.f;
    std::string xName;      // column, norm, colormap, or symbol, depending on kind
    std::string yName;
};

// nullopt means the line is not a command.
std::optional<Command> ParseCommand(const std::string& line);
