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
};

struct Uniforms {
    float projection[16];
    float pointSize;
    float viewportW;
    float viewportH;
    float _pad;
};

struct BrushColor {
    float r, g, b;
    float a = 1.0f;  // per-brush opacity (1.0 = use default, lower = additive composite)
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
    // Called when user presses C or I keys
    std::function<void()> onClearRequested;
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
    void UpdateUniforms();
    void Render();
    void Cleanup();

    void ScreenToWorld(int sx, int sy, float& wx, float& wy);

    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouse(wxMouseEvent& event);
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
    WGPUBuffer m_histUniformBuffer = nullptr;
    WGPUBindGroup m_histBindGroup = nullptr;
    WGPUTextureFormat m_surfaceFormat = WGPUTextureFormat_Undefined;

    bool m_initialized = false;

    // Point data
    std::vector<PointVertex> m_points;
    std::vector<float> m_basePositions;
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
    bool m_showGridLines = false;
    std::vector<float> m_gridXPositions;  // clip space positions
    std::vector<float> m_gridYPositions;
    WGPUBuffer m_gridLineBuffer = nullptr;
    size_t m_gridLineVertexCount = 0;

    // Display settings
    bool m_showUnselected = true;
    float m_pointSize = 6.0f;
    float m_opacity = 0.05f;

    // View state
    float m_panX = 0.0f, m_panY = 0.0f;
    float m_zoom = 1.0f;

    // Mouse interaction
    bool m_panning = false;
    bool m_selecting = false;
    bool m_translating = false;  // option+drag moves existing selection
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
