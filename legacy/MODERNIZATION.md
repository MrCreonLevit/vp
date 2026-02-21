# Viewpoints Modernization Project

## Project Overview

**Original Application**: Viewpoints - Fast interactive linked plotting of large multivariate data sets
**Original Authors**: Creon Levit & Paul Gazis
**Original Era**: ~2006-2015
**Current Status**: Legacy C++/FLTK/OpenGL codebase requiring modernization

---

## Vision & Goals

### Primary Objectives
- [ ] Resurrect and modernize the viewpoints interactive visualization tool
- [ ] Preserve core functionality: fast linked scatter plot exploration of large datasets
- [ ] Update to modern development practices and toolchains
- [ ] Improve maintainability and extensibility
- [ ] [Add your specific goals here]

### Success Criteria
- [ ] Application builds and runs on modern systems (macOS, Linux, Windows)
- [ ] Core visualization features work as originally designed
- [ ] Can handle large datasets (millions of points) efficiently
- [ ] Code is well-documented and maintainable
- [ ] [Add your criteria here]

---

## Current State Assessment

### Codebase Statistics
- **Total C++ Source**: ~40KB of core code (11 main source files)
- **Embedded Data**: ~20MB of sprite texture data (sprite_textures.cpp)
- **Lines of Code**:
  - vp.cpp: 2,633 lines (main entry point)
  - plot_window.cpp: 2,860 lines (OpenGL rendering)
  - control_panel_window.cpp: 30,489 lines (GUI controls)
  - data_file_manager.cpp: 3,075 lines (file I/O)
  - Vp_File_Chooser.cpp: 2,693 lines (custom file dialog)

### Language & Standards
- **C++ Era**: C++98/03 (confirmed)
  - No auto, lambdas, range-for, nullptr, or move semantics
  - No smart pointers (294 raw pointers found)
  - Heavy use of legacy template libraries (Blitz++)

### Dependencies (All Versions Confirmed)
- **FLTK 1.1.6** ⚠️ CRITICAL - Current version is 1.4+ (major migration needed)
- **FLEWS 0.3** - Custom FLTK extensions (likely unmaintained)
- **Blitz++ 0.9** ⚠️ - Unmaintained template library for arrays (should replace)
- **GSL 1.6+** ✓ - Still actively maintained, can keep
- **CFITSIO** ✓ - Still actively maintained, can keep
- **BOOST serialization** - Used only for deprecated config file feature
- **OpenGL 1.2-1.3** ⚠️ - Fixed-function pipeline, no shaders

### Architecture Analysis
- **Design Pattern**: Monolithic with heavy global state
- **Global Variables**: 30+ global arrays and pointers
  - `GLOBAL Plot_Window *pws[MAXPLOTS]` (256 max)
  - `GLOBAL blitz::Array<float,2> points` (main data)
  - `GLOBAL Brush *brushes[NBRUSHES]` (7 brushes)
- **Data Capacity**:
  - Max 400 columns (MAXVARS)
  - Max 8 million rows (MAXPOINTS)
  - Max 256 plot windows (MAXPLOTS)
- **Memory Management**: All manual (new/delete)
- **Error Handling**: Mixed (53 try/catch blocks, 48 return codes)

### Code Quality Assessment
**Strengths:**
- Extensive documentation and comments
- Sophisticated algorithms (14 normalization modes, order statistics)
- Performance optimizations (VBOs, deferred refresh, lazy evaluation)
- 54 different point sprite symbols with alpha blending

**Weaknesses:**
- C++98/03 patterns throughout (no modern C++)
- Zero smart pointers, all raw pointers
- Heavy circular dependencies between modules
- Platform-specific #ifdef code (79 occurrences)
- Static initialization order dependencies
- No unit tests found

### Build System
- **Primary**: Makefile (SVN-aware, platform-specific)
- **Targets**: macOS (primary), Linux (secondary), Windows (legacy)
- **Compiler Flags**: -O3/-O6, -Wconversion, -Wno-deprecated-declarations
- **Post-build**: Creates macOS .app bundle via shell script
- **Issues**: Hardcoded paths to FLEWS source (`../flews-0.3.1`)

### Platform-Specific Code
- macOS: AGL, Cocoa, ApplicationServices frameworks
- Linux: GL_GLEXT_PROTOTYPES flag required
- Windows: Legacy Dev-C++ references, outdated paths

### Known Issues from Code Analysis
- Configuration file saving marked as "DEPRECATED due to instability"
- Edit Column Labels tool described as unstable in comments
- No print command (screenshot-based workflow)
- Window manager refresh issues on macOS
- Keyboard shortcuts don't work in some contexts
- "Add to selection" mode reported as buggy
- Axis normalization resets other axes unexpectedly

---

## Architecture Strategy

### Language & Toolchain Decisions
**Recommendations based on codebase analysis:**

- [ ] **C++ Standard**: Target C++17 minimum (C++20 preferred)
  - **Rationale**: Need std::optional, structured bindings, filesystem library
  - **Impact**: Enables smart pointers, RAII, move semantics throughout
  - **Effort**: Medium - mostly compatible with C++98/03 patterns

- [ ] **Compiler**: GCC 9+, Clang 10+, MSVC 2019+
  - All support C++17 well, C++20 moderately
  - Existing -Wconversion warnings should be addressed

- [ ] **Build System**: Migrate to **CMake**
  - **Pros**: Better dependency management, cross-platform, IDE support
  - **Cons**: Learning curve, need to rewrite Makefile logic
  - **Effort**: Medium - straightforward translation from Makefile

- [ ] **Package Management**: **vcpkg** (recommended) or Conan
  - Handles FLTK, GSL, CFITSIO dependencies
  - Cross-platform package resolution
  - Integrates well with CMake

### GUI Framework Options
**Critical Decision - Current FLTK 1.1.6 is 15+ years old**

- [ ] **Option 1**: Update FLTK to 1.4+
  - **Pros**: Minimal code changes, preserves original look/feel, lightweight
  - **Cons**: Still relatively niche, API changes needed, custom widgets need updates
  - **Effort**: Low-Medium
  - **Migration Path**: Incremental, can test along the way

- [x] **Option 2**: Migrate to wxWidgets 3.2+ ⭐ **SELECTED**
  - **Pros**: Modern, mature, excellent cross-platform (native look/feel), good OpenGL integration (wxGLCanvas), active community, LGPL-friendly license
  - **Cons**: Significant rewrite needed, learning curve, larger than FLTK
  - **Effort**: High (several weeks to months)
  - **Best For**: Professional cross-platform applications with native UI
  - **OpenGL Support**: wxGLCanvas provides excellent integration
  - **Migration Strategy**:
    - Replace Fl_Window with wxFrame
    - Replace Fl_Gl_Window with wxGLCanvas
    - Replace FLTK widgets with wx equivalents (wxSlider, wxChoice, wxButton, etc.)
    - Rework event handling to wxWidgets event system
    - Custom widgets (file chooser, color chooser) replaced with wx native dialogs

- [ ] **Option 3**: Dear ImGui (Immediate-mode GUI)
  - **Pros**: Perfect for scientific visualization, minimal state management, OpenGL native
  - **Cons**: Complete UI rewrite, different interaction model
  - **Effort**: High (complete rewrite)
  - **Best For**: If starting fresh with modern OpenGL

- [ ] **Option 4**: Web-based (Electron + WebGL or WebAssembly)
  - **Pros**: Cross-platform, modern stack, GPU compute via WebGPU
  - **Cons**: Complete rewrite, performance concerns, large distribution
  - **Effort**: Very High (6+ months)
  - **Best For**: Cloud-based or collaborative version

- [x] **Decision**: wxWidgets 3.2+ chosen for native cross-platform UI with excellent OpenGL support

### Graphics Backend Options
**Current: OpenGL 1.2-1.3 fixed-function pipeline (no shaders)**
**Critical Issue: OpenGL deprecated on macOS (frozen at 4.1, no future support)**

- [ ] **Option 1**: OpenGL 3.3+ core profile
  - **REJECTED**: Deprecated on macOS, not future-proof
  - Would work on Linux/Windows but primary target is macOS

- [ ] **Option 2**: Metal (macOS native)
  - **Pros**: Best macOS performance, Apple's recommended path
  - **Cons**: macOS-only, no cross-platform support
  - **Effort**: High
  - **Rejected**: Not cross-platform, limited wxWidgets integration

- [ ] **Option 3**: Vulkan + MoltenVK
  - **Pros**: Cross-platform, high performance, industry standard
  - **Cons**: Extremely verbose, steep learning curve
  - **Effort**: Very High
  - **Rejected**: Too complex for resurrection project

- [x] **Option 4**: WebGPU via Dawn (Google's C++ implementation) ⭐ **SELECTED**
  - **Pros**:
    - Modern, future-proof API (W3C standard)
    - Maps to Metal (macOS), Vulkan (Linux), DirectX 12 (Windows)
    - Native C++ library (not browser-based)
    - Compute shaders for GPU-accelerated data processing
    - Excellent for large vertex buffers (millions of points)
    - Active development by Google/Chrome team
  - **Cons**:
    - Newer API (learning curve)
    - Need custom wxWindow integration (no wxGLCanvas equivalent yet)
  - **Effort**: High but worthwhile - modern graphics from day one
  - **Performance**:
    - Handles 8M+ vertices easily (96 MB buffer = trivial)
    - Compute shaders: normalize/rank 8M points in ~1-2ms (vs 100ms+ CPU)
    - Instanced rendering for efficient point sprites
  - **Integration**: Custom wxWindow subclass with Dawn surface

- [x] **Decision**: WebGPU via Dawn for native cross-platform graphics with Metal backend on macOS

### Data Processing Libraries
**Current: Blitz++ for arrays, GSL for statistics**

- [ ] **Replace Blitz++ with Eigen3 or std::vector** ⭐ RECOMMENDED
  - **Blitz++**: Unmaintained since 2005, slow compilation, template-heavy
  - **Eigen3**: Modern, actively maintained, excellent performance, cleaner API
  - **std::vector**: Built-in, sufficient for simple arrays
  - **Effort**: Medium - ~40 occurrences of `blitz::Array` to replace
  - **Impact**: Faster compilation, better error messages, modern C++ compatibility

- [ ] **Keep GSL** ✓
  - Still actively maintained, excellent for statistics/random numbers
  - Used for: percentile computation, Gaussianization, random sampling
  - **Alternative**: Could use C++17 <random> for some features

- [ ] **Keep CFITSIO** ✓
  - Still standard for FITS file I/O in astronomy
  - Minimal code changes needed

- [ ] **Add Modern Formats** (Phase 4+)
  - HDF5 (scientific data standard)
  - Apache Arrow/Parquet (columnar formats, excellent for large datasets)
  - NumPy .npy files (Python interop)

### Memory Management Strategy
**Current: Manual new/delete throughout (294 raw pointers)**

**Phase 1-2 Conversion Plan:**
1. Replace owning raw pointers with `std::unique_ptr`
2. Replace shared ownership with `std::shared_ptr` (rare in this codebase)
3. Non-owning pointers remain raw (e.g., FLTK widget callbacks)
4. Use RAII for OpenGL resources (texture/VBO wrappers)
5. Replace global arrays with application class members

**Memory Estimation:**
- 8 million points × 400 columns × 4 bytes = 12.8 GB
- Current limits reasonable for modern hardware
- Consider memory-mapped files for very large datasets (Phase 4)

### Error Handling Strategy
**Current: Mixed exceptions + return codes**

**Standardization Plan:**
1. Use exceptions for file I/O, parsing errors (already partially done)
2. Use std::optional/std::expected (C++23) for fallible operations
3. Remove BOOST serialization code (deprecated anyway)
4. Add structured logging (spdlog library)
5. Replace `abort()` in CHECK_GL_ERROR with exception

### Code Modernization Priorities
1. **Phase 1**: Get it building with modern dependencies
   - Update FLTK 1.1.6 → 1.4
   - Replace Blitz++ with Eigen3
   - Create CMake build system
   - Fix compilation errors

2. **Phase 2**: Modernize C++ to C++17
   - Smart pointers throughout
   - Extract global state into Application class
   - RAII for OpenGL resources
   - Replace raw arrays with std::vector/std::array

3. **Phase 3**: Refactor architecture
   - Dependency injection for major components
   - Event system to replace global callbacks
   - Modularize rendering pipeline
   - Add unit tests (Google Test)

4. **Phase 4**: Modernize graphics
   - OpenGL 3.3+ core profile
   - Write shaders for point rendering
   - GPU-accelerated data transformations

5. **Phase 5**: New features and enhancements
   - Modern file formats
   - Performance optimizations
   - UI/UX improvements
   - Fix known bugs

---

## Collaboration Guidelines

### Workflow Between Human & AI
- **Investigation**: Claude explores codebase, reports findings
- **Planning**: Discuss approaches before major changes
- **Implementation**: Iterative development with frequent check-ins
- **Testing**: Validate changes against original functionality
- **Documentation**: Keep this file and code docs updated

### Decision Making Process
1. Identify issue or opportunity
2. Claude researches options and provides recommendations
3. Human makes final decision
4. Document decision in "Technical Decisions Log" below
5. Implement and validate

### Git Commit Strategy
- Clear, descriptive commit messages
- Logical grouping of changes
- Co-authored commits: `Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>`
- Keep original code in git history for reference

### Communication Preferences
- [Add your preferences here - e.g., level of detail, pacing, etc.]

---

## Modern Architecture Design

### Technology Stack (Final Decision)

**Language & Build:**
- C++17 (minimum) or C++20 (preferred)
- CMake 3.20+ build system
- vcpkg for package management

**UI Framework:**
- wxWidgets 3.2+ (native macOS controls)

**Graphics:**
- WebGPU via Dawn (Google's native implementation)
- Maps to Metal on macOS automatically
- WGSL shaders (WebGPU Shading Language)

**Data Processing:**
- Eigen3 for linear algebra / matrix operations
- GSL for statistical functions
- CFITSIO for FITS file I/O

**Target Platforms:**
1. macOS (primary) - Metal backend via Dawn
2. Linux (secondary) - Vulkan backend via Dawn
3. Windows (optional) - DirectX 12 backend via Dawn

### Application Architecture

```
ViewpointsApp (wxApp)
  └── MainFrame (wxFrame)
      ├── wxMenuBar
      │   ├── File (Open, Save, Merge, Append, Quit)
      │   ├── View (Add/Remove Rows/Cols, Reload)
      │   ├── Tools (Statistics, Options)
      │   └── Help
      │
      ├── wxSplitterWindow
      │   ├── ControlPanel (wxNotebook)
      │   │   ├── PlotControlPage × N (per plot)
      │   │   │   ├── Axis selection (wxChoice)
      │   │   │   ├── Normalization (wxChoice)
      │   │   │   ├── Display options (wxCheckBox)
      │   │   │   └── Visual params (wxSlider)
      │   │   └── BrushControlPage × 7 (per brush)
      │   │       ├── Size/opacity (wxSlider)
      │   │       ├── Color (wxColourPickerCtrl)
      │   │       └── Symbol selection (wxChoice)
      │   │
      │   └── PlotGrid (wxPanel)
      │       └── WebGPUPlotCanvas × N (custom wxWindow)
      │           └── Dawn/WebGPU rendering
      │
      └── wxStatusBar

DataManager (singleton or DI)
  ├── Load/Save data (ASCII, Binary, FITS)
  ├── Column metadata
  └── Selection state arrays

RenderEngine (WebGPU/Dawn wrapper)
  ├── Device/Queue management
  ├── Pipeline creation
  ├── Buffer management
  ├── Shader compilation
  └── Compute pipeline (GPU transforms)
```

### WebGPU Integration Details

**Custom wxWindow for WebGPU:**
```cpp
class WebGPUPlotCanvas : public wxWindow {
public:
    WebGPUPlotCanvas(wxWindow* parent);

    // Dawn/WebGPU objects
    wgpu::Instance instance;
    wgpu::Surface surface;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::SwapChain swapChain;

    // Rendering resources
    wgpu::RenderPipeline pointRenderPipeline;
    wgpu::ComputePipeline normalizePipeline;
    wgpu::Buffer vertexBuffer;  // Millions of points
    wgpu::Buffer indexBuffer;   // Per-brush selection

    void OnPaint(wxPaintEvent& evt);
    void OnSize(wxSizeEvent& evt);
    void OnMouse(wxMouseEvent& evt);

private:
    void InitWebGPU();
    void CreatePipelines();
    void Render();
};
```

**Shader Pipeline (WGSL):**
```wgsl
// Vertex shader
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
    @location(2) size: f32,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @builtin(point_size) size: f32,
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = uniforms.projection * uniforms.view * vec4f(input.position, 1.0);
    output.color = input.color;
    output.size = input.size;
    return output;
}

// Fragment shader (point sprites)
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Sample sprite texture
    let uv = input.position.xy / textureSize;
    let sprite = textureSample(spriteTexture, spriteSampler, uv);
    return sprite * input.color;
}

// Compute shader (GPU normalization)
@compute @workgroup_size(256)
fn normalize_data(
    @builtin(global_invocation_id) id: vec3u
) {
    let idx = id.x;
    if (idx >= arrayLength(&inputData)) { return; }

    let value = inputData[idx];
    let normalized = (value - params.min) / (params.max - params.min);
    outputData[idx] = normalized;
}
```

### Data Flow Architecture

**Loading Data:**
```
File (ASCII/Binary/FITS)
  ↓ DataManager::LoadFile()
  ↓ Parse and validate
  ↓ Store in Eigen::MatrixXf (CPU)
  ↓ Upload to WebGPU buffer
  ↓ GPU storage buffer
```

**Real-time Interaction:**
```
User selects brush region (mouse drag)
  ↓ WebGPUPlotCanvas::OnMouse()
  ↓ Compute shader: mark selected points
  ↓ Update index buffer (selected indices)
  ↓ Render pipeline: draw by brush
  ↓ Update linked plots (selection shared)
```

**GPU-Accelerated Transformations:**
```
User changes normalization (wxChoice)
  ↓ Event handler → RenderEngine::Normalize()
  ↓ Compute shader dispatch (8M points in 1-2ms)
  ↓ Update vertex buffer
  ↓ Trigger re-render all plots
```

### Migration Strategy: Clean Rebuild

**NOT incrementally migrating old code:**
- ✗ Don't try to compile FLTK 1.1.6 + Blitz++ + old OpenGL
- ✗ Don't port line-by-line from old codebase

**DO build fresh with modern stack:**
- ✓ Study algorithms in old code (normalization, selection logic)
- ✓ Extract core math and logic (GSL usage, ranking algorithms)
- ✓ Rebuild UI from scratch with wxWidgets
- ✓ Rebuild rendering from scratch with WebGPU
- ✓ Use old code as reference specification

**Reference vs. Port:**
- `data_file_manager.cpp` → Study parsing logic, reimplement cleanly
- `plot_window.cpp` → Extract rendering math, translate to WGSL shaders
- `control_panel_window.cpp` → Understand UI layout, rebuild with wxWidgets sizers
- `sprite_textures.cpp` → Extract 54 sprite textures, convert to WebGPU texture array

---

## Development Roadmap

### Phase 0: Assessment & Planning ✓ (COMPLETED)
- [x] Read and understand existing codebase structure
- [x] Document dependencies and build requirements
- [x] Identify critical algorithms and features to preserve
- [x] Finalize architecture decisions (wxWidgets + WebGPU via Dawn)
- [x] Create MODERNIZATION.md strategy document

---

### Phase 1: Project Setup & Hello World (Week 1)

**Goal:** Minimal wxWidgets + WebGPU application running on macOS

**Tasks:**
- [ ] Create Git repository structure
  ```
  viewpoints-modern/
  ├── CMakeLists.txt
  ├── src/
  │   ├── main.cpp
  │   ├── ViewpointsApp.h/cpp
  │   └── MainFrame.h/cpp
  ├── shaders/
  ├── data/
  └── docs/
  ```

- [ ] Set up CMake build system
  - [ ] Find/configure wxWidgets 3.2+
  - [ ] Find/configure Dawn (via vcpkg or manual build)
  - [ ] Set C++17 standard
  - [ ] Configure macOS bundle creation

- [ ] Create minimal wxApp
  ```cpp
  class ViewpointsApp : public wxApp {
      virtual bool OnInit() override;
  };
  ```

- [ ] Create main window with menu bar
  ```cpp
  class MainFrame : public wxFrame {
      // File, View, Tools, Help menus
  };
  ```

- [ ] Test: Application launches, shows empty window with menus

**Deliverable:** Empty wxWidgets application that builds and runs

---

### Phase 2: WebGPU Integration (Week 2)

**Goal:** Custom wxWindow rendering with Dawn/WebGPU

**Tasks:**
- [ ] Create WebGPUCanvas class (inherit wxWindow)
- [ ] Initialize Dawn instance
- [ ] Create WebGPU surface from native window handle
  ```cpp
  #ifdef __WXMAC__
      // Use NSView to create Metal surface
      wgpu::SurfaceDescriptorFromMetalLayer metalDesc;
  #endif
  ```
- [ ] Request adapter and device
- [ ] Set up swap chain
- [ ] Implement basic render loop
  - OnPaint event → render frame
  - Clear to solid color
- [ ] Add WebGPUCanvas to MainFrame

- [ ] Test: Window displays WebGPU-rendered content (solid color)

**Deliverable:** wxWindow successfully rendering via WebGPU/Metal

---

### Phase 3: Basic Point Rendering (Week 3)

**Goal:** Render a simple dataset as points

**Tasks:**
- [ ] Create vertex buffer with test data (1000 random points)
  ```cpp
  struct Vertex {
      float x, y, z;
      uint32_t color;  // RGBA packed
  };
  ```

- [ ] Write basic WGSL shaders
  - Vertex shader: transform points to clip space
  - Fragment shader: solid color output

- [ ] Create render pipeline
  - Vertex buffer layout
  - Primitive topology: point-list
  - Depth/stencil state

- [ ] Implement Render() method
  - Begin render pass
  - Set pipeline and buffers
  - Draw points
  - Submit command buffer

- [ ] Add pan/zoom with mouse
  - Track mouse drag for pan
  - Mouse wheel for zoom
  - Update uniform buffer with view matrix

- [ ] Test: Can see and interact with 1000 points

**Deliverable:** Interactive point cloud viewer (basic)

---

### Phase 4: Data Loading (Week 4)

**Goal:** Load real data files (ASCII format first)

**Tasks:**
- [ ] Create DataManager class
  - Parse ASCII files (reference: data_file_manager.cpp)
  - Handle headers, comments, delimiters
  - Store in Eigen::MatrixXf

- [ ] Implement File → Open menu action
  - wxFileDialog for file selection
  - Load data into DataManager
  - Upload to GPU vertex buffer

- [ ] Display column metadata
  - Show number of rows/columns in status bar
  - Column labels

- [ ] Test with sampledata.txt from old repo
  - Verify parsing correctness
  - Display full dataset

- [ ] Handle large files (1M+ points)
  - Progress dialog during load
  - Efficient upload to GPU

**Deliverable:** Can load and display real scientific datasets

---

### Phase 5: Point Sprites & Brushes (Week 5-6)

**Goal:** Implement symbolic rendering and selection brushes

**Tasks:**
- [ ] Extract sprite textures from old sprite_textures.cpp
  - 54 symbol types (circles, crosses, diamonds, etc.)
  - Convert to single texture array or atlas
  - Upload to WebGPU texture

- [ ] Update shaders for sprite rendering
  - Sample from texture in fragment shader
  - Support alpha blending
  - Per-point size attribute

- [ ] Implement Brush class
  - 7 brushes with colors, sizes, symbols
  - Selection state array (per point)
  - Index buffer per brush (selected points)

- [ ] Implement rectangular selection
  - Mouse drag creates selection box
  - Compute shader: mark points inside box
  - Update index buffers

- [ ] Render by brush
  - Multiple draw calls (one per brush)
  - Draw unselected points in gray
  - Draw selected points with brush colors

- [ ] Test: Select points, assign to brushes, see colors

**Deliverable:** Full brush-based selection and rendering

---

### Phase 6: Control Panel (Week 7-8)

**Goal:** Build wxWidgets control panel for plot configuration

**Tasks:**
- [ ] Create ControlPanel class (wxNotebook)
- [ ] Create PlotControlPage
  - X/Y/Z axis selection (wxChoice with column names)
  - Normalization dropdown (14 types)
  - Visual parameter sliders (point size, background, etc.)
  - Display option checkboxes (axes, grid, histogram)

- [ ] Create BrushControlPage (7 tabs)
  - Color picker (wxColourPickerCtrl)
  - Size/opacity sliders
  - Symbol selection
  - Selection mode buttons

- [ ] Wire up events
  - Axis change → update vertex buffer with new columns
  - Normalization change → trigger GPU compute shader
  - Visual params → update uniform buffer
  - Brush settings → update render pipeline

- [ ] Layout with wxSizers
  - Responsive layout
  - Proper spacing and alignment

- [ ] Test: Control panel fully functional

**Deliverable:** Complete control panel with live updates

---

### Phase 7: Data Normalization & Transforms (Week 9)

**Goal:** Implement all 14 normalization modes via GPU

**Tasks:**
- [ ] Study normalization algorithms (reference: plot_window.cpp)
  - Min-max, zero-max, three-sigma
  - Trim percentiles (1e-2, 1e-3)
  - Log, rank, Gaussianize

- [ ] Implement compute shaders for each mode
  - WGSL compute pipeline
  - Parallel processing of millions of points
  - Output to staging buffer

- [ ] Implement ranking/sorting on GPU
  - Bitonic sort or radix sort
  - For rank and Gaussianize modes

- [ ] Implement 2D transforms
  - Sum vs. difference (x+y, x-y)
  - Conditional probability
  - Fluctuation analysis

- [ ] Performance test
  - Benchmark 8M point normalization
  - Target: <5ms on modern GPU

**Deliverable:** All normalization modes working efficiently

---

### Phase 8: Multi-Plot Grid (Week 10)

**Goal:** Support multiple linked plots in grid layout

**Tasks:**
- [ ] Create PlotGrid class (wxPanel + wxGridSizer)
  - Dynamic grid (2×2, 3×3, etc.)
  - Add/remove rows and columns
  - Manage multiple WebGPUCanvas instances

- [ ] Implement linked selection
  - Selection shared across all plots
  - Update all plots when any selection changes
  - Shared brush state

- [ ] Update control panel
  - Add tabs for each plot
  - Keep plot-to-tab mapping
  - Add/remove tabs dynamically

- [ ] Implement View menu actions
  - Add/Remove Row/Column
  - Restore default layout
  - Reload plots

- [ ] Test: 3×3 grid with linked brushing

**Deliverable:** Multi-plot visualization with linking

---

### Phase 9: File I/O & Remaining Features (Week 11-12)

**Goal:** Complete feature parity with original application

**Tasks:**
- [ ] Implement all file formats
  - ASCII (done in Phase 4)
  - Binary (row-major, column-major)
  - FITS (via CFITSIO)

- [ ] Implement file operations
  - Save (ASCII/Binary/FITS)
  - Append data (add rows)
  - Merge data (add columns)
  - Save selected points only

- [ ] Implement histograms
  - Marginal histograms
  - Selection histograms
  - Conditional probability histograms
  - Render as overlays on plots

- [ ] Implement statistics window
  - Show selection stats
  - Mean, std dev, percentiles
  - Update in real-time

- [ ] Implement keyboard shortcuts
  - wxAcceleratorTable
  - 'i' = invert, 'c' = clear, 'x' = kill, etc.

**Deliverable:** Feature-complete application

---

### Phase 10: Polish & Testing (Week 13-14)

**Goal:** Production quality and stability

**Tasks:**
- [ ] Add unit tests (Google Test)
  - Data loading/parsing
  - Normalization correctness
  - Selection algorithms

- [ ] Integration tests
  - Full workflow tests
  - File format compatibility

- [ ] Fix known bugs from original
  - Window refresh issues (likely fixed with WebGPU)
  - Keyboard shortcut problems
  - Selection mode bugs

- [ ] Performance optimization
  - Profile with Instruments (macOS)
  - Optimize GPU pipeline
  - Reduce CPU overhead

- [ ] Memory leak detection
  - Leaks instrument on macOS
  - Proper WebGPU resource cleanup

- [ ] Error handling
  - Graceful failures
  - User-friendly error messages
  - Logging system

**Deliverable:** Stable, tested, production-ready application

---

### Phase 11: Documentation & Distribution (Week 15-16)

**Goal:** Release preparation

**Tasks:**
- [ ] User documentation
  - Update user manual (HTML or Markdown)
  - Feature descriptions
  - Keyboard shortcuts reference
  - Example workflows

- [ ] Developer documentation
  - Code documentation (Doxygen)
  - Architecture overview
  - Build instructions
  - Contributing guide

- [ ] Create macOS .app bundle
  - Bundle frameworks and libraries
  - Code signing
  - Notarization for Gatekeeper
  - DMG installer

- [ ] Optional: Linux AppImage
  - Bundle all dependencies
  - Single-file distribution

- [ ] Set up CI/CD (GitHub Actions)
  - Automated builds on push
  - Run tests
  - Create release artifacts

- [ ] Create release
  - Tag version 3.0.0 (major modernization)
  - Release notes
  - Binary distribution

**Deliverable:** Released application ready for users

---

## Technical Decisions Log

### Decision Template
```
**Date**: YYYY-MM-DD
**Decision**: [Short title]
**Context**: [Why this decision was needed]
**Options Considered**: [What alternatives were evaluated]
**Decision**: [What was chosen and why]
**Consequences**: [Expected impact]
```

### Decisions

#### Strategic Architecture Decision
**Date**: 2026-02-19
**Decision**: Clean rebuild with wxWidgets + WebGPU (Dawn) instead of incremental migration
**Context**:
- OpenGL deprecated on macOS (frozen at 4.1, no future)
- Legacy dependencies (FLTK 1.1.6, Blitz++) difficult to build on modern systems
- C++98 codebase needs complete modernization anyway
- Primary target platform is macOS

**Options Considered**:
1. Incremental migration: Update FLTK → 1.4, modernize OpenGL → 3.3+
   - Rejected: Still uses deprecated OpenGL on macOS
   - Building old dependencies likely to fail

2. Port to Qt + Vulkan
   - Rejected: Vulkan extremely verbose, steep learning curve

3. Port to wxWidgets + WebGPU via Dawn
   - **SELECTED**: Modern, future-proof, excellent for large datasets

**Decision**: Build fresh application with:
- **UI**: wxWidgets 3.2+ (native macOS controls)
- **Graphics**: WebGPU via Dawn (maps to Metal on macOS)
- **Data**: Eigen3 (replace Blitz++), GSL, CFITSIO
- **Language**: C++17/20
- **Build**: CMake + vcpkg

**Rationale**:
- WebGPU is future-proof (W3C standard, active development)
- Dawn automatically maps to Metal (macOS), Vulkan (Linux), DX12 (Windows)
- Compute shaders enable GPU-accelerated normalization/ranking (100x faster)
- Excellent support for large vertex buffers (8M+ points)
- Clean modern codebase vs. maintaining legacy code
- Use old code as **reference specification**, not migration source

**Consequences**:
- Higher initial effort (rebuild vs. port)
- Modern, maintainable codebase from day one
- Future-proof graphics API
- Better performance (GPU compute)
- No dependency on deprecated technologies
- 15-16 week development timeline estimated

---

## Notes & References

### wxWidgets Migration Strategy

**Component Mapping (FLTK → wxWidgets):**

| FLTK Component | wxWidgets Equivalent | Notes |
|----------------|----------------------|-------|
| Fl_Window | wxFrame | Top-level window |
| Fl_Gl_Window | wxGLCanvas | OpenGL rendering context |
| Fl_Tabs | wxNotebook | Tabbed interface |
| Fl_Button | wxButton | Standard button |
| Fl_Choice | wxChoice | Dropdown menu |
| Fl_Slider | wxSlider | Slider control |
| Fl_Value_Input | wxSpinCtrlDouble | Numeric input with spinner |
| Fl_Menu_Bar | wxMenuBar | Menu bar |
| Fl_Color_Chooser | wxColourDialog | Native color picker |
| Fl_File_Chooser | wxFileDialog | Native file dialog |
| Fl_Input | wxTextCtrl | Text input |
| Fl_Check_Button | wxCheckBox | Checkbox |

**Event System Migration:**
- FLTK callbacks → wxWidgets event tables or Bind()
- `Fl::run()` → `wxApp::OnInit()` + event loop
- Custom event handling via `EVT_*` macros or `Bind()`

**OpenGL Integration:**
- Replace `Fl_Gl_Window` inheritance with `wxGLCanvas`
- Use wxGLContext for context management
- Double buffering via `SwapBuffers()`
- Similar to current FLTK implementation

**Key Migration Tasks:**
1. Create wxApp-derived application class
2. Convert all window classes to wxFrame/wxDialog
3. Convert Plot_Window to inherit from wxGLCanvas
4. Rebuild control panel with wxWidgets sizers (layout management)
5. Replace custom file/color choosers with wx native dialogs
6. Convert FLTK callbacks to wxWidgets events
7. Update Makefile/CMake to link wxWidgets

**Advantages of wxWidgets for This Project:**
- Native OpenGL support via wxGLCanvas (minimal changes to rendering code)
- Rich set of standard controls (reduces custom widget code)
- Native file/color dialogs (eliminate custom Vp_File_Chooser, Vp_Color_Chooser)
- Better layout management (sizers vs. manual positioning)
- Active development and good documentation
- Cross-platform without #ifdef madness
- LGPL license compatible with open source

### Useful File Locations
- Main entry point: `vp.cpp` → will become wxApp class
- Plot window implementation: `plot_window.cpp` → migrate to wxGLCanvas
- Control panel: `control_panel_window.cpp` → rebuild with wxNotebook + sizers
- Data file handling: `data_file_manager.cpp` → minimal changes (logic independent)
- OpenGL rendering: `sprite_textures.cpp`, `brush.cpp` → no changes needed
- Build configuration: `Makefile`, `include_libraries_vp.h` → migrate to CMake
- Documentation: `README`, `vp_help_manual.htm`, `INSTALL`
- Developer notes: `Notes.creon`, `BUGS`

### External Resources

**Core Technologies:**
- wxWidgets: https://www.wxwidgets.org/
  - Docs: https://docs.wxwidgets.org/stable/
  - Tutorial: https://docs.wxwidgets.org/stable/page_introduction.html

- WebGPU / Dawn:
  - WebGPU Spec: https://www.w3.org/TR/webgpu/
  - Dawn (Google implementation): https://dawn.googlesource.com/dawn
  - WGSL Spec: https://www.w3.org/TR/WGSL/
  - WebGPU Samples: https://webgpu.github.io/webgpu-samples/
  - Learn WGSL: https://google.github.io/tour-of-wgsl/

- Eigen3: https://eigen.tuxfamily.org/
  - Getting Started: https://eigen.tuxfamily.org/dox/GettingStarted.html

**Scientific Libraries:**
- GSL: https://www.gnu.org/software/gsl/
- CFITSIO: https://heasarc.gsfc.nasa.gov/fitsio/

**Build & Tools:**
- CMake: https://cmake.org/
- vcpkg: https://vcpkg.io/

**Tutorials & Guides:**
- WebGPU Fundamentals: https://webgpufundamentals.org/
- Learn WebGPU (C++): https://eliemichel.github.io/LearnWebGPU/
- wxWidgets + Custom Rendering: https://wiki.wxwidgets.org/Drawing_on_a_panel_with_a_DC

### Original Documentation
See `README` for original usage documentation and feature descriptions.

---

## Questions & Open Issues

- [ ] What is the target user base for the resurrected application?
- [ ] Are there specific scientific use cases to prioritize?
- [ ] What level of backward compatibility is needed?
- [ ] What platforms are highest priority?
- [ ] [Add your questions here]

---

---

## Next Steps - Getting Started

### Immediate Actions (This Week)

1. **Set up development environment**
   - [ ] Install Xcode Command Line Tools (macOS)
   - [ ] Install CMake (brew install cmake)
   - [ ] Install vcpkg package manager
   - [ ] Install wxWidgets via vcpkg or Homebrew

2. **Build Dawn/WebGPU**
   - [ ] Clone Dawn repository
   - [ ] Build Dawn for macOS (Metal backend)
   - [ ] Test Dawn samples to verify installation

3. **Create project repository**
   - [ ] Initialize new Git repo (viewpoints-modern)
   - [ ] Create initial directory structure
   - [ ] Add .gitignore (build/, .DS_Store, etc.)
   - [ ] Set up CMakeLists.txt skeleton

4. **Phase 1 Week 1 Tasks**
   - [ ] Create minimal wxApp hello world
   - [ ] Verify it builds and runs
   - [ ] Add to version control

### Questions to Resolve Before Starting

- [ ] Repository name: "viewpoints-modern" or "viewpoints2" or other?
- [ ] License: Keep original license or update?
- [ ] Version numbering: Start at 3.0.0 (major modernization)?
- [ ] Should we preserve git history from old repo or start fresh?

### Key Risks & Mitigations

**Risk 1: Dawn/WebGPU integration complexity**
- Mitigation: Start with Dawn samples, incremental integration
- Fallback: If too difficult, could fall back to OpenGL 4.1 (deprecated but works)

**Risk 2: wxWidgets learning curve**
- Mitigation: Use wxWidgets samples and documentation extensively
- Fallback: Well-documented, mature framework with good community

**Risk 3: Compute shader complexity**
- Mitigation: Start with CPU normalization, add GPU compute in Phase 7
- Not critical path for MVP

**Risk 4: Performance with millions of points**
- Mitigation: Early performance testing in Phase 3
- WebGPU designed for this use case

### Success Metrics

**Phase 1 Success (Week 2):**
- wxWidgets app builds on macOS
- WebGPU renders something (clear screen or triangle)

**MVP Success (Week 8):**
- Can load ASCII data files
- Can render millions of points
- Basic brush selection works
- Control panel modifies display

**Feature Parity (Week 12):**
- All original features working
- Better performance than original
- No deprecated APIs

---

**Last Updated**: 2026-02-19
**Status**: Planning complete - Ready to begin Phase 1
