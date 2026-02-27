# Viewpoints

**Fast interactive linked scatter plots for exploring large multivariate datasets.**

Viewpoints is a tool for visual exploration of high-dimensional data. It displays multiple scatter plots in a configurable grid, with **linked brushing** — selecting points in one plot instantly highlights the same data rows across all other plots. This makes it easy to discover correlations, clusters, and outliers across many variables simultaneously.

Originally developed by Creon Levit and Paul Gazis at NASA, Viewpoints has been modernized with a contemporary graphics and UI stack while preserving the interactive exploration workflow of the original.

## Features

### Data Visualization
- **Multi-plot grid** — configurable NxM grid of scatter plots, each showing a different pair of variables; **draggable dividers** between plots to resize rows and columns independently
- **Linked brushing** — select points in any plot, see them highlighted everywhere
- **7 simultaneous brushes** — each with independent color, opacity offset, symbol, and size; "All" button to apply changes to every brush at once
- **10 procedural point symbols** — circle, square, diamond, triangles, cross, plus, star, ring, outlines (SDF-based, crisp at any zoom)
- **Additive blending** — dense regions glow brighter, revealing structure in overplotted data
- **9 density-based colormaps** — Viridis, Plasma, Inferno, Turbo, Hot, Cool, Grayscale, Blue-Red (viewport-aware density recomputation on zoom/pan)
- **Marginal histograms** — staircase outlines along each axis, adjustable bins, per-plot toggle
- **Grid lines** at nice values with aligned tick labels
- **Hover datapoint details** — hover over a point to see all variable values in a popup (toggle with **T** key or global checkbox; on by default)
- **Selection rectangle** with real-time coordinate and percentage display
- **Status bar** — persistent display of dataset info and selection count with percentage
- **Automatic point sizing and opacity** — default point size and opacity scale with dataset size

### Data Handling
- **ASCII/CSV/TSV** file loading with progress bar
- **Apache Parquet** file loading via Arrow C++ (optional dependency)
- **Command-line loading** — `vp -i data.csv` or `vp data.parquet`
- **Constant-column removal** — columns with a single value are automatically dropped on load
- **Save All / Save Selected** — export data or just brushed points as Parquet or CSV
- **10 normalization modes** — Min-Max, +only, Max |val|, Trim percentile, 3 Sigma, Log10, Arctan, Rank, Gaussianize (per-plot, per-axis, smart defaults)
- **Large dataset subsampling** — datasets over 4M points are automatically subsampled per plot for GPU memory management
- Tested with datasets up to **12.6 million rows** (NYC taxi data)

### Interaction
- **Drag** to select points with a rectangle
- **Cmd+drag** to extend selection (add to existing)
- **Option+drag** to translate/move the selection rectangle
- **Shift+drag** or right-drag to pan
- **Scroll** (two-finger) to pan
- **Scroll wheel** to zoom centered on cursor position
- **Trackpad pinch** to zoom centered on pinch point
- **Axis linking** — link a variable so panning/zooming propagates to all plots sharing it; **Opt+scroll** to independently scale axes
- **Double-click** any plot to quickly select a brush from a colored popup menu
- **Click axis labels** to change the variable via a popup menu
- **Defer Redraws** toggle for smooth interaction with very large datasets
- **Randomize Axes** button per plot
- **Kill Selected** — permanently remove selected points from dataset
- **3D rotation** — optional Z-axis with screen-space rotation sliders for both Y and X axes, spin/rock animation for each, and per-axis reset buttons; rotations always follow screen axes regardless of prior rotations (accumulated rotation matrix)
- **Arrow-key selection stepping** — step through individual points with arrow keys
- Keyboard shortcuts: **C** clear selection, **I** invert selection, **D** toggle deselected points, **K** kill selected, **T** toggle hover details, **R** reset active view, **Shift+R** reset all views, **Cmd+S** save all, **Cmd+Shift+S** save selected, **Q** quit

### Control Panel
- **Grid-based plot selector** matching the plot layout, plus a "Brushes + Global Controls" tab
- **Per-plot controls** — axis selectors, normalization, link, point size, opacity, histogram bins, show/hide unselected, grid lines, histograms, 3D rotation
- **Per-brush controls** — color picker with alpha (double-click), symbol selector, size offset, opacity offset; right-click to reset
- **Color map controls** — density-based colormaps with background brightness adjustment
- **Scrollable panels** for small windows

### Performance
- **GPU selection buffer** — selection state stored in a WebGPU storage buffer; the shader handles coloring via uniform lookup, eliminating CPU-side vertex buffer rebuilds on selection changes (~8x less CPU→GPU bandwidth)
- **GPU-accelerated rendering** — instanced quad rendering with SDF fragment shaders
- **Multiple render pipelines** — additive blending, alpha-blended overlays, histogram/grid/selection overlays

## Technology

| Component | Technology |
|-----------|-----------|
| Language | C++20 + Objective-C++ (macOS surface only) |
| GUI Framework | wxWidgets 3.3 |
| GPU Rendering | WebGPU via wgpu-native (Metal backend on macOS) |
| Data Files | CSV/TSV/ASCII + Apache Parquet (optional, via Arrow C++) |
| Build System | CMake |
| Platforms | macOS (Linux/Windows possible with platform surface code) |

## Building

### Prerequisites (macOS)

Required:
```bash
brew install wxwidgets wgpu-native cmake
```

Optional (for Parquet file support):
```bash
brew install apache-arrow
```

### Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

CMake will report whether Parquet support is enabled:
```
-- Parquet support: enabled
```

### Run

```bash
./build/vp.app/Contents/MacOS/vp
```

With a data file:
```bash
./build/vp.app/Contents/MacOS/vp -i data/sampledata.txt
./build/vp.app/Contents/MacOS/vp data.parquet
```

Or as a macOS app:
```bash
open build/vp.app
```

## Project Structure

```
vp/
├── CMakeLists.txt          Build configuration
├── README.md               This file
├── LICENSE                  MIT License
├── src/                    Modern source code
│   ├── main.cpp            Application entry point
│   ├── ViewpointsApp.*     wxApp subclass with command-line parsing
│   ├── MainFrame.*         Main window, grid layout, linked brushing
│   ├── WebGPUCanvas.*      GPU-rendered scatter plot canvas
│   ├── WebGPUContext.*     Shared WebGPU device/pipeline/shader resources
│   ├── ControlPanel.*      Tabbed control panel UI
│   ├── DataManager.*       File loading (CSV + Parquet) and data storage
│   ├── Normalize.*         11 normalization/transform modes
│   ├── ColorMap.*           9 density-based colormaps
│   ├── Brush.h             Brush color definitions
│   ├── VerticalLabel.h     Rotated Y-axis label widget
│   └── Info.plist.in       macOS app bundle metadata
├── shaders/                WGSL shader reference
├── data/                   Sample data files
│   ├── sampledata.txt      Inline skating EMG (29,900 rows)
│   ├── sampledata1000.txt  Smaller sample
│   └── nuclides/           IAEA nuclear properties (3,386 nuclides, 17 variables)
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
