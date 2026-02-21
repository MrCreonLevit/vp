# Viewpoints

**Fast interactive linked scatter plots for exploring large multivariate datasets.**

Viewpoints is a tool for visual exploration of high-dimensional data. It displays multiple scatter plots in a configurable grid, with **linked brushing** — selecting points in one plot instantly highlights the same data rows across all other plots. This makes it easy to discover correlations, clusters, and outliers across many variables simultaneously.

Originally developed by Creon Levit and Paul Gazis at NASA, Viewpoints has been modernized with a contemporary graphics and UI stack while preserving the interactive exploration workflow of the original.

## Features

### Data Visualization
- **Multi-plot grid** — configurable NxM grid of scatter plots, each showing a different pair of variables
- **Linked brushing** — select points in any plot, see them highlighted everywhere
- **7 simultaneous brushes** — each with independent color, opacity, symbol, and size
- **10 procedural point symbols** — circle, square, diamond, triangles, cross, plus, star, ring, outlines
- **Additive blending** — dense regions glow brighter, revealing structure in overplotted data
- **9 density-based colormaps** — Viridis, Plasma, Inferno, Turbo, Hot, Cool, Grayscale, Blue-Red (viewport-aware density recomputation on zoom/pan)
- **Marginal histograms** — staircase outlines along each axis, adjustable bins, per-plot toggle
- **Grid lines** at nice values with aligned tick labels

### Data Handling
- **ASCII/CSV/TSV** file loading with progress bar
- **Constant-column removal** — columns with a single value are automatically dropped on load
- **Save All / Save Selected** — export data or just brushed points as CSV
- **11 normalization modes** — None, Min-Max, Zero-Max, Max Abs, Trim percentile, Three Sigma, Log10, Arctan, Rank, Gaussianize (per-plot, per-axis)
- Tested with datasets up to **2.5 million rows** (Tycho-2 star catalog)

### Interaction
- **Drag** to select points with a rectangle (with real-time coordinate display)
- **Cmd+drag** to extend selection (add to existing)
- **Option+drag** to translate/move the selection rectangle
- **Shift+drag** or right-drag to pan
- **Scroll wheel** to zoom (independent X/Y zoom)
- **Axis lock** — lock a variable so panning/zooming propagates to all plots showing it
- **Randomize Axes** button per plot
- Keyboard shortcuts: **C** clear, **I** invert, **D** delete selected, **R** reset views, **Q** quit

### Control Panel
- **Grid-based plot selector** matching the plot layout, plus an "All" tab for global settings
- **Per-plot controls** — axis selectors, normalization, point size, opacity, histogram bins, show/hide unselected, grid lines, histograms
- **Per-brush controls** — color picker with alpha (double-click), symbol selector, size offset
- **Scrollable panels** for small windows

## Technology

| Component | Technology |
|-----------|-----------|
| Language | C++17 + Objective-C++ (macOS surface only) |
| GUI Framework | wxWidgets 3.3 |
| GPU Rendering | WebGPU via wgpu-native (Metal backend on macOS) |
| Build System | CMake |
| Platforms | macOS (Linux/Windows possible with platform surface code) |

All rendering uses the GPU via WebGPU with instanced quad rendering, signed distance function (SDF) point symbols in the fragment shader, and multiple render pipelines for additive blending, alpha-blended overlays, histograms, grid lines, and selection rectangles.

## Building

### Prerequisites (macOS)

```bash
brew install wxwidgets wgpu-native eigen gsl cfitsio cmake
```

### Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Run

```bash
./build/vp.app/Contents/MacOS/vp
```

Or:

```bash
open build/vp.app
```

## Project Structure

```
vp/
├── CMakeLists.txt          Build configuration
├── README.md               This file
├── src/                    Modern source code
│   ├── main.cpp            Application entry point
│   ├── ViewpointsApp.*     wxApp subclass
│   ├── MainFrame.*         Main window, grid layout, linked brushing
│   ├── WebGPUCanvas.*      GPU-rendered scatter plot canvas
│   ├── WebGPUContext.*     Shared WebGPU device/pipeline resources
│   ├── ControlPanel.*      Tabbed control panel UI
│   ├── DataManager.*       File loading and data storage
│   ├── Normalize.*         11 normalization/transform modes
│   ├── ColorMap.*           9 density-based colormaps
│   ├── Brush.h             Brush color definitions
│   ├── VerticalLabel.h     Rotated Y-axis label widget
│   └── Info.plist.in       macOS app bundle metadata
├── shaders/                WGSL shader reference
├── data/                   Sample data files
│   ├── sampledata.txt      Inline skating EMG (29,900 rows)
│   └── sampledata1000.txt  Smaller sample
└── legacy/                 Original C++98/FLTK source (reference)
```

## History

Viewpoints was originally written in C++98 with FLTK and OpenGL (2005–2015) for interactive exploration of NASA datasets. This modern rewrite (2025–2026) replaces the entire rendering and UI stack while preserving the core linked-brushing workflow.

## Authors

- **Creon Levit** — original author and modernization
- **Paul Gazis** — original co-author
- Modernization assisted by Claude (Anthropic)

## License

MIT License. See [LICENSE](LICENSE) for details.
