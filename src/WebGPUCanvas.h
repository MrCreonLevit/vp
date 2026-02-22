// Viewpoints (MIT License) - See LICENSE file
#pragma once

#include <wx/wx.h>
#include <webgpu.h>
#include <wgpu.h>
#include <vector>
#include <functional>

class WebGPUContext;

struct PointVertex {
    float x, y;
    float r, g, b, a;
    float symbol;     // 0=circle, 1=square, 2=diamond, 3=triangle, etc.
    float sizeScale;  // multiplier for point size (1.0 = default)
};

struct Uniforms {
    float projection[16];
    float pointSize;
    float viewportW;
    float viewportH;
    float _pad;
};

// Symbol types
enum PointSymbol {
    SYMBOL_CIRCLE = 0,
    SYMBOL_SQUARE,
    SYMBOL_DIAMOND,
    SYMBOL_TRIANGLE_UP,
    SYMBOL_TRIANGLE_DOWN,
    SYMBOL_CROSS,
    SYMBOL_PLUS,
    SYMBOL_STAR,
    SYMBOL_RING,
    SYMBOL_SQUARE_OUTLINE,
    SYMBOL_COUNT
};

const char* SymbolName(int symbol);

struct BrushColor {
    float r, g, b;
    float a = 1.0f;
    int symbol = SYMBOL_CIRCLE;
    float sizeOffset = 0.0f;  // additive point size offset for this brush (-10 to +20)
};

class WebGPUCanvas : public wxWindow {
public:
    WebGPUCanvas(wxWindow* parent, WebGPUContext* ctx, int plotIndex = 0);
    ~WebGPUCanvas() override;

    void SetPoints(std::vector<PointVertex> points);
    void SetAxisInfo(const std::string& xLabel, const std::string& yLabel,
                     float xDataMin, float xDataMax, float yDataMin, float yDataMax);
    void SetPointSize(float size);
    void SetOpacity(float alpha);
    void SetHistBins(int bins);
    void SetShowUnselected(bool show);
    void SetShowGridLines(bool show);
    void SetShowHistograms(bool show);
    void SetBackground(float brightness);
    void SetUseAdditiveBlending(bool additive);
    void SetColorMap(int colorMap);  // 0 = default, else ColorMapType
    void SetDeferRedraws(bool defer) { m_deferRedraws = defer; }
    void SetPanZoom(float panX, float panY, float zoomX, float zoomY);
    float GetPanX() const { return m_panX; }
    float GetPanY() const { return m_panY; }
    float GetZoomX() const { return m_zoomX; }
    float GetZoomY() const { return m_zoomY; }
    // Set explicit grid line positions in clip space [-1, 1]
    void SetGridLinePositions(const std::vector<float>& xPositions,
                              const std::vector<float>& yPositions);
    void SetBrushColors(const std::vector<BrushColor>& colors);
    void SetSelection(const std::vector<int>& sel);
    void ClearSelection();
    void InvertSelection();
    const std::vector<int>& GetSelection() const { return m_selection; }
    int GetSelectedCount() const;
    int GetPlotIndex() const { return m_plotIndex; }
    void SetActive(bool active);
    void ResetView();

    // Called when user completes a brush rectangle in this canvas.
    // Provides world-space rect so MainFrame can test rows and propagate.
    std::function<void(int plotIndex, float x0, float y0, float x1, float y1, bool extend)> onBrushRect;
    std::function<void()> onClearRequested;
    std::function<void()> onKillRequested;
    // Called when user pans or zooms this plot
    std::function<void(int plotIndex, float panX, float panY, float zoomX, float zoomY)> onViewChanged;
    // Called during selection drag with world-space box coordinates
    std::function<void(int plotIndex, float x0, float y0, float x1, float y1)> onSelectionDrag;
    std::function<void()> onInvertRequested;
    std::function<void()> onResetViewRequested;
    // Called on each render with current visible range in normalized coords
    std::function<void(int plotIndex, float xMin, float xMax, float yMin, float yMax)> onViewportChanged;

private:
    void InitSurface();
    void CreatePipeline();
    void ConfigureSurface(int width, int height);
    void UpdateVertexBuffer();
    void UpdatePointColors();
    void UpdateHistograms();
    void UpdateGridLines();
    void RecomputeDensityColors();
    void UpdateUniforms();
    void Render();
    void Cleanup();

    void ScreenToWorld(int sx, int sy, float& wx, float& wy);

    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouse(wxMouseEvent& event);
    void OnMagnify(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);

    WebGPUContext* m_ctx = nullptr;
    int m_plotIndex = 0;
    bool m_isActive = false;

    // Per-canvas WebGPU objects
    WGPUSurface m_surface = nullptr;
    WGPUSurfaceConfiguration m_surfaceConfig = {};
    WGPURenderPipeline m_pipeline = nullptr;       // additive blending for all/unselected
    WGPURenderPipeline m_selPipeline = nullptr;     // alpha blending for selected (on top)
    WGPUBuffer m_vertexBuffer = nullptr;
    WGPUBuffer m_selVertexBuffer = nullptr;
    size_t m_selVertexCount = 0;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    // GPU selection buffer (bind group 1)
    WGPUBuffer m_selectionGpuBuffer = nullptr;
    WGPUBuffer m_brushColorGpuBuffer = nullptr;
    WGPUBuffer m_brushParamsGpuBuffer = nullptr;
    WGPUBindGroup m_selectionBindGroup = nullptr;
    size_t m_selectionBufferSize = 0;
    WGPUBuffer m_histUniformBuffer = nullptr;
    WGPUBindGroup m_histBindGroup = nullptr;
    WGPUTextureFormat m_surfaceFormat = WGPUTextureFormat_Undefined;

    bool m_initialized = false;

    // Point data
    std::vector<PointVertex> m_points;
    std::vector<float> m_basePositions;
    std::vector<float> m_baseColors;  // original r,g,b per point (3 floats each)
    Uniforms m_uniforms = {};

    // Selection state (0 = unselected, 1-7 = brush index)
    std::vector<int> m_selection;
    std::vector<BrushColor> m_brushColors;

    // Histogram rendering
    WGPURenderPipeline m_histPipeline = nullptr;
    WGPUBuffer m_histBuffer = nullptr;
    size_t m_histVertexCount = 0;
    bool m_showHistograms = true;
    int m_histBins = 64;

    // Axis info for labels/ticks
    std::string m_xLabel, m_yLabel;
    float m_xDataMin = 0.0f, m_xDataMax = 1.0f;
    float m_yDataMin = 0.0f, m_yDataMax = 1.0f;

    // Grid lines
    bool m_deferRedraws = false;
    bool m_showGridLines = false;
    std::vector<float> m_gridXPositions;  // clip space positions
    std::vector<float> m_gridYPositions;
    WGPUBuffer m_gridLineBuffer = nullptr;
    size_t m_gridLineVertexCount = 0;

    // Display settings
    bool m_showUnselected = true;
    float m_bgBrightness = 0.0f;
    bool m_useAdditive = true;
    int m_colorMap = 0;
    float m_pointSize = 6.0f;
    float m_opacity = 0.05f;

    // View state (independent X and Y zoom for axis lock)
    float m_panX = 0.0f, m_panY = 0.0f;
    float m_zoomX = 1.0f, m_zoomY = 1.0f;

    // Selection rectangle rendering
    WGPUBuffer m_selRectBuffer = nullptr;
    size_t m_selRectVertexCount = 0;
    void UpdateSelectionRect();

    // Mouse interaction
    bool m_panning = false;
    bool m_selecting = false;
    bool m_translating = false;
    wxPoint m_lastMouse;
    wxPoint m_selectStart;
    wxPoint m_selectEnd;

    // Last selection rect in world space (for translating)
    float m_lastRectX0 = 0, m_lastRectY0 = 0;
    float m_lastRectX1 = 0, m_lastRectY1 = 0;
    bool m_hasLastRect = false;

    // Platform-specific
    void* m_metalLayer = nullptr;
};
