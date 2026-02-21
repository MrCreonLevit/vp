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
};

class WebGPUCanvas : public wxWindow {
public:
    WebGPUCanvas(wxWindow* parent, WebGPUContext* ctx, int plotIndex = 0);
    ~WebGPUCanvas() override;

    void SetPoints(std::vector<PointVertex> points);
    void SetPointSize(float size);
    void SetOpacity(float alpha);
    void SetBrushColor(const BrushColor& color);
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

private:
    void InitSurface();
    void CreatePipeline();
    void ConfigureSurface(int width, int height);
    void UpdateVertexBuffer();
    void UpdatePointColors();
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
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBuffer m_vertexBuffer = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUTextureFormat m_surfaceFormat = WGPUTextureFormat_Undefined;

    bool m_initialized = false;

    // Point data
    std::vector<PointVertex> m_points;
    std::vector<float> m_basePositions;
    Uniforms m_uniforms = {};

    // Selection state
    std::vector<int> m_selection;
    BrushColor m_brushColor = {1.0f, 0.3f, 0.3f};

    // Display settings
    float m_pointSize = 6.0f;
    float m_opacity = 0.05f;

    // View state
    float m_panX = 0.0f, m_panY = 0.0f;
    float m_zoom = 1.0f;

    // Mouse interaction
    bool m_panning = false;
    bool m_selecting = false;
    wxPoint m_lastMouse;
    wxPoint m_selectStart;
    wxPoint m_selectEnd;

    // Platform-specific
    void* m_metalLayer = nullptr;
};
